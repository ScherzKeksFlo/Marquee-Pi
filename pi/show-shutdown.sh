#!/bin/sh
set -eu

STATE_URL=http://127.0.0.1:8765/ui/state
SHOW_URL=http://127.0.0.1:8765/ui/shutdown
DATA_DIR=/var/lib/arcade-pi-display

state=$(curl --silent --show-error --fail --max-time 2 "$STATE_URL" 2>/dev/null || true)
case "$state" in
  *'"shutting_down": true'*) exit 0 ;;
esac

curl --silent --show-error --fail --max-time 2 --request POST "$SHOW_URL" >/dev/null 2>&1 || exit 0

delay=$(python3 - "$DATA_DIR" <<'PY'
import json
import subprocess
import sys
from pathlib import Path

data_dir = Path(sys.argv[1])
delay = 4.0
try:
    name = json.loads((data_dir / "shutdown.json").read_text(encoding="utf-8"))["name"]
    media = data_dir / Path(name).name
    if media.is_file() and media.suffix.lower() == ".mp4":
        result = subprocess.run(
            ["ffprobe", "-v", "error", "-show_entries", "format=duration",
             "-of", "default=nw=1:nk=1", str(media)],
            capture_output=True, text=True, timeout=5, check=False)
        delay = min(30.0, max(4.0, float(result.stdout.strip()) + 1.0))
except Exception:
    delay = 6.0
print(f"{delay:.2f}")
PY
)
sleep "$delay"
