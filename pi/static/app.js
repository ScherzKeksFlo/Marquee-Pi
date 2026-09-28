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
    }
  } catch (error) {
    console.error(error);
  }
}

stage.addEventListener("pointerdown", (event) => {
  event.preventDefault();
  stage.setPointerCapture(event.pointerId);
  recognizer.down(event.pointerId, event.clientX, event.clientY, performance.now());
});
stage.addEventListener("pointermove", (event) => {
  recognizer.move(event.pointerId, event.clientX, event.clientY);
});
stage.addEventListener("pointerup", (event) => {
  const kind = recognizer.up(event.pointerId, event.clientX, event.clientY, performance.now());
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
stage.addEventListener("pointercancel", (event) => recognizer.cancel(event.pointerId));

poll();
setInterval(poll, 1000);
