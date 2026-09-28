#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
  echo "Dieses Skript muss als root ausgeführt werden." >&2
  exit 1
fi

CMDLINE=/boot/firmware/cmdline.txt
BACKUP=/boot/firmware/cmdline.txt.marquee-pi-before-quiet-boot

if [ ! -f "$CMDLINE" ]; then
  echo "$CMDLINE wurde nicht gefunden." >&2
  exit 1
fi

if [ ! -e "$BACKUP" ]; then
  cp -p "$CMDLINE" "$BACKUP"
fi

python3 - "$CMDLINE" <<'PY'
import os
import sys
from pathlib import Path

path = Path(sys.argv[1])
tokens = path.read_text(encoding="utf-8").split()
remove = {
    "quiet", "splash", "logo.nologo", "vt.global_cursor_default=0",
    "systemd.show_status=false", "rd.systemd.show_status=false", "loglevel=3",
}
tokens = [token for token in tokens if token not in remove]
if "console=tty1" not in tokens:
    tokens.append("console=tty1")
tokens.extend([
    "quiet", "splash", "loglevel=3", "logo.nologo", "vt.global_cursor_default=0",
    "systemd.show_status=false", "rd.systemd.show_status=false",
])
if not tokens or not any(token.startswith("root=") and len(token) > 5 for token in tokens):
    raise SystemExit("Kernel-Befehlszeile ist leer oder enthält keinen root=-Eintrag; keine Änderung geschrieben.")
pending = path.with_name(path.name + ".marquee-pi-pending")
with pending.open("w", encoding="utf-8") as stream:
    stream.write(" ".join(tokens) + "\n")
    stream.flush()
    os.fsync(stream.fileno())
os.replace(pending, path)
PY

systemctl disable getty@tty1.service >/dev/null 2>&1 || true
echo "Der stille Boot ist eingerichtet und wird nach dem nächsten Neustart aktiv."
