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
  const stamp = "?v=" + state.version;
  if (state.shutting_down) {
    stage.appendChild(makeMedia("/ui/shutdown" + stamp, state.shutdown_video, false));
    return;
  }
  if (!state.game_title) {
    stage.appendChild(makeMedia("/ui/default" + stamp, state.default_video));
    return;
  }

  if (view === "default") {
    stage.appendChild(makeMedia("/ui/default" + stamp, state.default_video));
    return;
  }
  const artwork = {
    controls: state.has_controls,
    box_art: state.has_box_art,
    logo: state.has_logo,
    marquee: state.has_marquee
  };
  if (artwork[view]) {
    stage.appendChild(makeMedia("/ui/game/" + view + stamp, false));
  } else if (state.has_marquee) {
    stage.appendChild(makeMedia("/ui/game/marquee" + stamp, false));
  } else {
    stage.appendChild(makeMedia("/ui/default" + stamp, state.default_video));
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
    if (recognizer.longPress(performance.now())) openMenu();
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
    const action = currentState?.gesture_actions?.[kind] ?? "none";
    if (["marquee", "box_art", "logo", "controls", "default"].includes(action)) {
      view = action;
      render();
    } else if (action === "retroarch_menu") {
      fetch("/ui/gesture", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ kind })
      }).catch(console.error);
    }
  }
});
stage.addEventListener("pointercancel", (event) => {
  clearTimeout(longPressTimer);
  recognizer.cancel(event.pointerId);
});

// ---- Touch menu (long press) -------------------------------------------------

const menu = document.getElementById("menu");
const MENU_IDLE_MS = 20000;
const VIEWS = [
  ["marquee", "Marquee", () => true],
  ["box_art", "Box Art", (s) => s.has_box_art],
  ["logo", "Logo", (s) => s.has_logo],
  ["controls", "Controls", (s) => s.has_controls],
  ["default", "Standard", () => true]
];
let menuOpen = false;
let menuInfo = null;
let pendingPower = null;
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
  head.appendChild(el("span", "", "Marquee-Pi"));
  head.appendChild(button("✕", closeMenu, { className: "close" }));
  panel.appendChild(head);

  panel.appendChild(el("div", "section", "Ansicht"));
  const views = el("div", "row");
  for (const [name, label, available] of VIEWS) {
    const enabled = Boolean(state.game_title) && available(state);
    views.appendChild(button(label, () => {
      view = name;
      render();
      renderMenu();
    }, { disabled: !enabled, className: enabled && view === name ? "selected" : "" }));
  }
  panel.appendChild(views);

  if (info?.brightness?.supported) {
    panel.appendChild(el("div", "section", "Helligkeit"));
    const percent = info.brightness.percent;
    const row = el("div", "row");
    row.appendChild(button("−", () => changeBrightness(percent - 10)));
    row.appendChild(el("div", "level", percent + " %"));
    row.appendChild(button("+", () => changeBrightness(percent + 10)));
    panel.appendChild(row);
  }

  panel.appendChild(el("div", "section", "Status"));
  const status = el("div", "status");
  const addRow = (label, value) => {
    status.appendChild(el("span", "", label));
    status.appendChild(el("span", "", value));
  };
  addRow("Arcade-PC", info ? (info.client_connected ? "verbunden" : "nicht verbunden") : "…");
  addRow("Adresse", info?.addresses?.length ? info.addresses.join("  ") : "unbekannt");
  addRow("Spiel", state.game_title || "–");
  addRow("Version", info?.app_version ?? "…");
  panel.appendChild(status);

  if (info?.power_enabled) {
    panel.appendChild(el("div", "section", "System"));
    const power = el("div", "row");
    if (pendingPower) {
      const text = pendingPower === "reboot" ? "Pi wirklich neu starten?" : "Pi wirklich herunterfahren?";
      power.appendChild(el("div", "confirm", text));
      power.appendChild(button("Ja", () => runPower(pendingPower), { className: "danger" }));
      power.appendChild(button("Abbrechen", () => setPending(null)));
    } else {
      power.appendChild(button("Neustart", () => setPending("reboot"), { className: "danger" }));
      power.appendChild(button("Herunterfahren", () => setPending("poweroff"), { className: "danger" }));
    }
    panel.appendChild(power);
  }
  menu.replaceChildren(panel);
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

poll();
setInterval(poll, 1000);
