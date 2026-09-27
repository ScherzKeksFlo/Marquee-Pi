#!/bin/sh
set -eu

BROWSER=""
if command -v chromium-browser >/dev/null 2>&1; then
  BROWSER="$(command -v chromium-browser)"
elif command -v chromium >/dev/null 2>&1; then
  BROWSER="$(command -v chromium)"
else
  echo "Chromium is required for the display." >&2
  exit 1
fi

if command -v xset >/dev/null 2>&1; then
  xset s off
  xset s noblank
  xset -dpms 2>/dev/null || true
else
  echo "xset is required to disable X11 screen blanking." >&2
fi

if command -v xrandr >/dev/null 2>&1 && xrandr --query | grep -q '^Composite-1 connected' && xrandr --query | grep -q '^DSI-1 connected'; then
  xrandr --output Composite-1 --off --output DSI-1 --primary --auto
fi

exec "$BROWSER" --kiosk --no-first-run --noerrdialogs \
  --disable-session-crashed-bubble --disable-infobars \
  --disable-features=Translate,TranslateUI --disable-gpu --disable-gpu-compositing \
  --lang=de-DE \
  http://127.0.0.1:8765/ui/
