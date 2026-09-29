const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(path.join(__dirname, "../static/app.js"), "utf8");
const literal = /const TEXT = (\{[\s\S]*?\n\});/.exec(source);
assert.ok(literal, "TEXT table not found in app.js");
const TEXT = vm.runInNewContext("(" + literal[1] + ")");

assert.deepEqual(Object.keys(TEXT).sort(), ["de", "en"]);
assert.deepEqual(Object.keys(TEXT.de).sort(), Object.keys(TEXT.en).sort(),
  "English and German menu texts must define the same keys");
for (const language of Object.keys(TEXT)) {
  for (const [key, value] of Object.entries(TEXT[language])) {
    assert.ok(typeof value === "string" && value.length > 0, `${language}.${key} is empty`);
  }
}

// Every key the code looks up, directly or through the VIEWS table, must exist.
const used = new Set([...source.matchAll(/\bt\("([a-z_]+)"\)/g)].map((match) => match[1]));
for (const match of source.matchAll(/\["[a-z_]+", "(view_[a-z_]+)"/g)) used.add(match[1]);
assert.ok(used.size > 10, "expected to find the menu keys in use");
for (const key of used) assert.ok(key in TEXT.en, `t("${key}") is not defined`);

console.log("i18n tests passed");
