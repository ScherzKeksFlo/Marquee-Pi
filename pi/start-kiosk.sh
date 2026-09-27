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

exec "$BROWSER" --kiosk --no-first-run --noerrdialogs \
  --disable-session-crashed-bubble --disable-infobars \
  http://127.0.0.1:8765/ui/
