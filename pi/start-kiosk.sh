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

HERE="$(cd "$(dirname "$0")" && pwd)"

if command -v xset >/dev/null 2>&1; then
  xset s off
  xset s noblank
  xset -dpms 2>/dev/null || true
else
  echo "xset is required to disable X11 screen blanking." >&2
fi

# The display profile comes from the local API (config.json is not readable for this user).
# Defaults describe an installation without a profile: nothing is changed except the old
# DSI/Composite handling below.
MP_CONFIGURED=0 MP_OUTPUT="" MP_MODE="" MP_ROTATE=normal MP_MATRIX="1 0 0 0 1 0 0 0 1"
MP_SCALE=auto MP_TOUCH_DEVICE=""
fetch_profile() {
  tries=0
  while [ "$tries" -lt 20 ]; do
    if profile="$(curl -fsS --max-time 2 http://127.0.0.1:8765/ui/display-profile 2>/dev/null)"; then
      printf '%s\n' "$profile"
      return 0
    fi
    tries=$((tries + 1))
    sleep 0.5
  done
  return 1
}
if profile="$(fetch_profile)"; then
  eval "$profile"
else
  echo "Display profile unavailable; using the defaults." >&2
fi

if command -v xrandr >/dev/null 2>&1; then
  outputs="$(xrandr --query || true)"
  if [ -n "$MP_OUTPUT" ] && printf '%s\n' "$outputs" | grep -q "^$MP_OUTPUT connected"; then
    # Exactly one screen is used; the others stay off so that the window cannot land on them.
    printf '%s\n' "$outputs" | awk '/ connected/ { print $1 }' | while read -r name; do
      [ "$name" = "$MP_OUTPUT" ] || xrandr --output "$name" --off || true
    done
    if [ -n "$MP_MODE" ]; then
      xrandr --output "$MP_OUTPUT" --primary --mode "$MP_MODE" --rotate "$MP_ROTATE" \
        || xrandr --output "$MP_OUTPUT" --primary --auto --rotate "$MP_ROTATE" || true
    else
      xrandr --output "$MP_OUTPUT" --primary --auto --rotate "$MP_ROTATE" || true
    fi
  elif [ -n "$MP_OUTPUT" ]; then
    echo "Configured screen $MP_OUTPUT is not connected; leaving the outputs as they are." >&2
  elif [ "$MP_CONFIGURED" = 0 ] && printf '%s\n' "$outputs" | grep -q '^Composite-1 connected' \
       && printf '%s\n' "$outputs" | grep -q '^DSI-1 connected'; then
    xrandr --output Composite-1 --off --output DSI-1 --primary --auto
  fi
fi

# Touch follows the rotation of the picture.
if [ -n "$MP_TOUCH_DEVICE" ] && command -v xinput >/dev/null 2>&1; then
  # shellcheck disable=SC2086  # the matrix is nine numbers
  xinput set-prop "$MP_TOUCH_DEVICE" "Coordinate Transformation Matrix" $MP_MATRIX \
    || echo "Could not set the touch matrix for $MP_TOUCH_DEVICE." >&2
fi

# One knob scales the menu and the gesture distances: the short side of the screen is 480 CSS px.
SCALE_FLAG=""
if [ "$MP_CONFIGURED" = 1 ]; then
  scale="$MP_SCALE"
  if [ "$scale" = auto ]; then
    size="$(xrandr --query 2>/dev/null | sed -n 's/.*current \([0-9]*\) x \([0-9]*\).*/\1 \2/p' | head -n 1)"
    # shellcheck disable=SC2086
    scale="$(python3 "$HERE/display_profile.py" --scale $size 2>/dev/null || echo 1)"
  fi
  SCALE_FLAG="--force-device-scale-factor=$scale"
fi

# shellcheck disable=SC2086
exec "$BROWSER" --kiosk --no-first-run --noerrdialogs \
  --disable-session-crashed-bubble --disable-infobars \
  --disable-features=Translate,TranslateUI --disable-gpu --disable-gpu-compositing \
  --lang=en-US $SCALE_FLAG \
  http://127.0.0.1:8765/ui/
