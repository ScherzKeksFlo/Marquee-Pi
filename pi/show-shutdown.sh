#!/bin/sh
set -eu

STATE_URL=http://127.0.0.1:8765/ui/state
SHOW_URL=http://127.0.0.1:8765/ui/shutdown

if systemctl list-jobs --no-legend --no-pager 2>/dev/null | \
    grep -Eq '(^|[[:space:]])(reboot|kexec|soft-reboot)\.target([[:space:]]|$)'; then
  exit 0
fi

state=$(curl --silent --show-error --fail --max-time 2 "$STATE_URL" 2>/dev/null || true)
if [ -z "$state" ]; then
  exit 0
fi

parsed=$(printf '%s' "$state" | python3 -c '
import json
import sys
try:
    state = json.load(sys.stdin)
    print("1" if state.get("shutting_down") is True else "0", float(state.get("shutdown_delay", 4)))
except (ValueError, TypeError):
    raise SystemExit(1)
' 2>/dev/null) || exit 0
set -- $parsed
[ "$#" -eq 2 ] || exit 0

if [ "$1" = 1 ]; then
  exit 0
fi
delay=$2

curl --silent --show-error --fail --max-time 2 --request POST "$SHOW_URL" >/dev/null 2>&1 || exit 0
sleep "$delay"
