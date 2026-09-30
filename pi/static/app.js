"use strict";

const stage = document.getElementById("stage");
const recognizer = new GestureRecognizer();
let lastVersion = -1;
let currentState = null;
let view = "marquee";

function makeMedia(url, video, loop = true) {
  const element = document.createElement(video ? "video" : "img");
  element.src = url;
  element.addEventListener("error", () => {
    if (url.startsWith("/ui/default")) {
      element.replaceWith(makeMedia("/ui/fallback.svg", false));
    } else if (!url.endsWith("/ui/fallback.svg")) {
      element.replaceWith(makeMedia("/ui/default", currentState?.default_video ?? false));
    }
  }, { once: true });
  if (video) {
    element.autoplay = true;
    element.muted = true;
    element.loop = loop;
    element.playsInline = true;
  } else {
    element.alt = "";
  }
  return element;
}

function render() {
  if (!currentState) return;
  stage.replaceChildren();
  const state = currentState;
  // Cache keys come from content (file name / hash), not the state version, which
  // restarts at 1 with the server while the browser keeps media for a year.
  const defaultStamp = "?v=" + (state.default_name ?? "none");
  const gameStamp = (kind) => "?v=" + (state.game_hashes?.[kind] ?? "none");
  if (state.shutting_down) {
    stage.appendChild(makeMedia("/ui/shutdown" + "?v=" + (state.shutdown_name ?? "none"), state.shutdown_video, false));
    return;
  }
  if (!state.game_title) {
    stage.appendChild(makeMedia("/ui/default" + defaultStamp, state.default_video));
    return;
  }

  if (view === "default") {
    stage.appendChild(makeMedia("/ui/default" + defaultStamp, state.default_video));
    return;
  }
  const artwork = {
    controls: state.has_controls,
    box_art: state.has_box_art,
    logo: state.has_logo,
    marquee: state.has_marquee
  };
  if (artwork[view]) {
    stage.appendChild(makeMedia("/ui/game/" + view + gameStamp(view), false));
  } else if (state.has_marquee) {
    stage.appendChild(makeMedia("/ui/game/marquee" + gameStamp("marquee"), false));
  } else {
    stage.appendChild(makeMedia("/ui/default" + defaultStamp, state.default_video));
    const label = document.createElement("div");
    label.id = "name";
    label.textContent = state.game_title;
    stage.appendChild(label);
  }
}

async function poll() {
  try {
    const response = await fetch("/ui/state", { cache: "no-store" });
    if (!response.ok) throw new Error("State unavailable");
    const state = await response.json();
    if (state.version !== lastVersion) {
      currentState = state;
      lastVersion = state.version;
      view = "marquee";
      recognizer.reset();
      render();
      if (state.shutting_down) closeMenu();
      else renderMenu();
    }
  } catch (error) {
    console.error(error);
  }
}

stage.addEventListener("pointerdown", (event) => {
  event.preventDefault();
  stage.setPointerCapture(event.pointerId);
  recognizer.down(event.pointerId, event.clientX, event.clientY, performance.now());
  clearTimeout(longPressTimer);
  longPressTimer = setTimeout(() => {
    if (currentState?.shutting_down || gestureAction("long-press") === "none") return;
    if (recognizer.longPress(performance.now())) runGesture("long-press");
  }, recognizer.longPressMs + 30);
});
stage.addEventListener("pointermove", (event) => {
  recognizer.move(event.pointerId, event.clientX, event.clientY);
});
stage.addEventListener("pointerup", (event) => {
  clearTimeout(longPressTimer);
  const kind = recognizer.up(event.pointerId, event.clientX, event.clientY, performance.now());
  if (currentState?.shutting_down) return;
  if (kind === "tap" && currentState?.game_title && currentState.has_controls) {
    view = view === "marquee" ? "controls" : "marquee";
    render();
  } else if (kind && kind.startsWith("swipe-")) {
    runGesture(kind);
  }
});
stage.addEventListener("pointercancel", (event) => {
  clearTimeout(longPressTimer);
  recognizer.cancel(event.pointerId);
});

// ---- Gesture actions and touch menu ------------------------------------------

const VIEW_ACTIONS = ["marquee", "box_art", "logo", "controls", "default"];

function gestureAction(kind) {
  const fallback = kind === "long-press" ? "touch_menu" : "none";
  return currentState?.gesture_actions?.[kind] ?? fallback;
}

// Performs whatever action is assigned to a recognised swipe or long press.
function runGesture(kind) {
  const action = gestureAction(kind);
  if (VIEW_ACTIONS.includes(action)) {
    view = action;
    render();
  } else if (action === "touch_menu") {
    openMenu();
  } else if (action === "retroarch_menu") {
    postJson("/ui/gesture", { kind }).catch(console.error);
  }
}

const menu = document.getElementById("menu");
const MENU_IDLE_MS = 20000;
const VIEWS = [
  ["marquee", "view_marquee", () => true],
  ["box_art", "view_box_art", (s) => s.has_box_art],
  ["logo", "view_logo", (s) => s.has_logo],
  ["controls", "view_controls", (s) => s.has_controls],
  ["default", "view_default", () => true]
];

// Menu texts. The Windows app picks the language and the Pi reports it in the state.
const TEXT = {
  en: {
    view_marquee: "Marquee", view_box_art: "Box Art", view_logo: "Logo", view_controls: "Controls",
    view_default: "Default", section_view: "View", section_brightness: "Brightness", section_status: "Status",
    arcade_pc: "Arcade PC", connected: "connected", disconnected: "not connected", address: "Address",
    unknown: "unknown", game: "Game", core: "Core", rom: "ROM", version: "Version", system_button: "System …", system: "System",
    back: "‹ Back", restart: "Restart", shutdown: "Shut down", yes: "Yes", cancel: "Cancel",
    confirm_restart: "Really restart the Pi?", confirm_shutdown: "Really shut down the Pi?"
  },
  de: {
    view_marquee: "Marquee", view_box_art: "Box Art", view_logo: "Logo", view_controls: "Controls",
    view_default: "Standard", section_view: "Ansicht", section_brightness: "Helligkeit", section_status: "Status",
    arcade_pc: "Arcade-PC", connected: "verbunden", disconnected: "nicht verbunden", address: "Adresse",
    unknown: "unbekannt", game: "Spiel", core: "Core", rom: "ROM", version: "Version", system_button: "System …", system: "System",
    back: "‹ Zurück", restart: "Neustart", shutdown: "Herunterfahren", yes: "Ja", cancel: "Abbrechen",
    confirm_restart: "Pi wirklich neu starten?", confirm_shutdown: "Pi wirklich herunterfahren?"
  }
};

function t(key) {
  return (TEXT[currentState?.language] ?? TEXT.en)[key] ?? TEXT.en[key] ?? key;
}
let menuOpen = false;
let menuInfo = null;
let pendingPower = null;
let menuPage = "main";
let menuIdleTimer = null;
let menuRefreshTimer = null;
let longPressTimer = null;

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function button(label, onTap, options = {}) {
  const node = el("button", options.className, label);
  node.type = "button";
  node.disabled = Boolean(options.disabled);
  node.addEventListener("click", () => {
    bumpMenuIdle();
    onTap();
  });
  return node;
}

function postJson(path, payload) {
  return fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload)
  });
}

function bumpMenuIdle() {
  clearTimeout(menuIdleTimer);
  menuIdleTimer = setTimeout(closeMenu, MENU_IDLE_MS);
}

function renderMenu() {
  if (!menuOpen) return;
  const state = currentState ?? {};
  const info = menuInfo;
  const panel = el("div", "panel");

  const head = el("div", "head");
  if (menuPage === "system") {
    head.appendChild(button(t("back"), () => { setPending(null); setPage("main"); }, { className: "back" }));
    head.appendChild(el("span", "title", t("system")));
    head.appendChild(button("✕", closeMenu, { className: "close" }));
    panel.appendChild(head);
    const systemBody = el("div", "body");
    const power = el("div", "row big");
    if (pendingPower) {
      const text = pendingPower === "reboot" ? t("confirm_restart") : t("confirm_shutdown");
      power.appendChild(el("div", "confirm", text));
      power.appendChild(button(t("yes"), () => runPower(pendingPower), { className: "danger yes" }));
      power.appendChild(button(t("cancel"), () => setPending(null)));
    } else {
      power.appendChild(button(t("restart"), () => setPending("reboot"), { className: "danger" }));
      power.appendChild(button(t("shutdown"), () => setPending("poweroff"), { className: "danger" }));
    }
    systemBody.appendChild(power);
    panel.appendChild(systemBody);
    menu.replaceChildren(panel);
    return;
  }
  head.appendChild(el("span", "title", "Marquee-Pi"));
  if (info?.power_enabled) head.appendChild(button(t("system_button"), () => setPage("system"), { className: "system" }));
  head.appendChild(button("✕", closeMenu, { className: "close" }));
  panel.appendChild(head);
  const body = el("div", "body");
  panel.appendChild(body);

  body.appendChild(el("div", "section", t("section_view")));
  const views = el("div", "row");
  for (const [name, label, available] of VIEWS) {
    const enabled = Boolean(state.game_title) && available(state);
    views.appendChild(button(t(label), () => {
      view = name;
      render();
      renderMenu();
    }, { disabled: !enabled, className: enabled && view === name ? "selected" : "" }));
  }
  body.appendChild(views);

  if (info?.brightness?.supported) {
    body.appendChild(el("div", "section", t("section_brightness")));
    const percent = info.brightness.percent;
    const row = el("div", "row");
    row.appendChild(button("−", () => changeBrightness(percent - 10)));
    row.appendChild(el("div", "level", percent + " %"));
    row.appendChild(button("+", () => changeBrightness(percent + 10)));
    body.appendChild(row);
  }

  body.appendChild(el("div", "section", t("section_status")));
  const status = el("div", "status");
  const addRow = (label, value, tone) => {
    status.appendChild(el("span", "", label));
    const cell = el("span", "");
    if (tone) cell.appendChild(el("i", "dot " + tone));
    cell.appendChild(document.createTextNode(value));
    status.appendChild(cell);
  };
  addRow(t("arcade_pc"), info ? (info.client_connected ? t("connected") : t("disconnected")) : "…",
         info ? (info.client_connected ? "ok" : "bad") : "");
  addRow(t("address"), info?.addresses?.length ? info.addresses.join("  ") : t("unknown"));
  addRow(t("game"), state.game_title || "–");
  if (state.game_title && state.game_core) addRow(t("core"), state.game_core);
  if (state.game_title && state.game_rom) addRow(t("rom"), state.game_rom);
  addRow(t("version"), info?.app_version ?? "…");
  body.appendChild(status);

  menu.replaceChildren(panel);
}

function setPage(page) {
  menuPage = page;
  renderMenu();
}

let pendingTimer = null;
function setPending(action) {
  pendingPower = action;
  clearTimeout(pendingTimer);
  if (action) pendingTimer = setTimeout(() => setPending(null), 8000);
  renderMenu();
}

async function refreshMenuInfo() {
  try {
    const response = await fetch("/ui/system", { cache: "no-store" });
    if (response.ok) menuInfo = await response.json();
  } catch (error) {
    console.error(error);
  }
  renderMenu();
}

async function changeBrightness(percent) {
  const target = Math.max(5, Math.min(100, percent));
  try {
    const response = await postJson("/ui/brightness", { percent: target });
    if (response.ok && menuInfo) menuInfo.brightness.percent = (await response.json()).percent;
  } catch (error) {
    console.error(error);
  }
  renderMenu();
}

async function runPower(action) {
  setPending(null);
  try {
    const response = await postJson("/ui/power", { action });
    if (response.ok) closeMenu();
  } catch (error) {
    console.error(error);
  }
}

function openMenu() {
  if (menuOpen || currentState?.shutting_down) return;
  menuOpen = true;
  menu.hidden = false;
  menuInfo = null;
  pendingPower = null;
  menuPage = "main";
  bumpMenuIdle();
  renderMenu();
  refreshMenuInfo();
  menuRefreshTimer = setInterval(refreshMenuInfo, 2000);
}

function closeMenu() {
  menuOpen = false;
  menu.hidden = true;
  menu.replaceChildren();
  pendingPower = null;
  clearTimeout(menuIdleTimer);
  clearTimeout(pendingTimer);
  clearInterval(menuRefreshTimer);
}

menu.addEventListener("click", (event) => {
  if (event.target === menu) closeMenu();
});

// Load the menu fonts up front so the first opening does not flash fallback text.
if (document.fonts) {
  for (const face of ['400 12px "Silkscreen"', '400 19px "Chakra Petch"', '600 18px "Chakra Petch"',
                      '700 18px "Chakra Petch"', '400 15px "IBM Plex Mono"', '500 24px "IBM Plex Mono"']) {
    document.fonts.load(face).catch(() => {});
  }
}

poll();
setInterval(poll, 1000);
