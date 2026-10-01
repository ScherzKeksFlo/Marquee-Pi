const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const test = require("node:test");
const assert = require("node:assert/strict");

// Runs the real static/app.js against a minimal DOM and a scripted /ui/state.
function loadApp(replies) {
  const node = () => ({
    children: [], style: { setProperty() {} }, classList: { toggle() {}, add() {}, remove() {} },
    addEventListener() {}, appendChild(child) { this.children.push(child); },
    replaceChildren(...children) { this.children = children; }, replaceWith() {},
    setAttribute() {}, remove() {}, set textContent(value) { this.text = value; },
  });
  const nodes = {};
  const context = vm.createContext({
    document: {
      getElementById: (id) => (nodes[id] ??= node()), createElement: node,
      documentElement: node(), body: node(), addEventListener() {},
    },
    window: { innerWidth: 800, innerHeight: 480, devicePixelRatio: 1, addEventListener() {} },
    navigator: { language: "en" }, performance: { now: () => 0 }, console,
    GestureRecognizer: class { reset() {} down() {} move() {} up() {} longPress() {} },
    fetch: async (url) => (url === "/ui/state"
      ? { ok: true, json: async () => replies.shift() }
      : { ok: true, json: async () => ({}) }),
    setTimeout() { return 0; }, clearTimeout() {}, setInterval() { return 0; }, clearInterval() {},
  });
  for (const name of ["i18n.js"]) {
    const file = path.join(__dirname, "..", "static", name);
    if (fs.existsSync(file)) vm.runInContext(fs.readFileSync(file, "utf8"), context);
  }
  const source = fs.readFileSync(path.join(__dirname, "..", "static", "app.js"), "utf8")
    .replace(/poll\(\);\s*setInterval\(poll, 1000\);\s*$/, "");
  vm.runInContext(source, context);
  return {
    poll: () => vm.runInContext("poll()", context),
    get: (expression) => vm.runInContext(expression, context),
    stage: nodes.stage ?? context.document.getElementById("stage"),
  };
}

const state = (extra) => ({ version: 2, instance_id: "a", game_title: "Old game", has_marquee: true,
                            game_hashes: { marquee: "old" }, ...extra });

test("a new server instance with the same version replaces the shown state", async () => {
  const app = loadApp([state(), state({ instance_id: "b", game_title: "New game", game_hashes: { marquee: "new" } })]);
  await app.poll();
  assert.equal(app.stage.children[0].src, "/ui/game/marquee?v=old");
  await app.poll();
  assert.equal(app.get("currentState.game_title"), "New game");
  assert.equal(app.stage.children[0].src, "/ui/game/marquee?v=new");
});

test("the same instance and version does not render again", async () => {
  const app = loadApp([state(), state({ game_title: "Ignored" })]);
  await app.poll();
  const shown = app.stage.children[0];
  await app.poll();
  assert.equal(app.get("currentState.game_title"), "Old game");
  assert.equal(app.stage.children[0], shown);
});

test("a shutdown state of a new instance closes the touch menu despite a version collision", async () => {
  const app = loadApp([state(), state({ instance_id: "b", game_title: null, shutting_down: true, shutdown_name: "s.png" })]);
  await app.poll();
  app.get("menuOpen = true");
  await app.poll();
  assert.equal(app.get("menuOpen"), false);
  assert.equal(app.stage.children[0].src, "/ui/shutdown?v=s.png");
});
