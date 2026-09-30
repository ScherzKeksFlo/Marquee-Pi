#!/usr/bin/env python3
"""marquee-pi-configure-display: find the screens, pick one and write the display profile.

    marquee-pi-configure-display             interactive: detect, choose, test picture, save
    marquee-pi-configure-display --auto      no questions: keep a profile or derive one from what is connected
    marquee-pi-configure-display --json      print what was detected and exit
    marquee-pi-configure-display --revert    undo the boot configuration changes made by this tool

Detection only reads /sys and /proc. The boot configuration is changed only after the user
confirmed it (or written to a text file instead); see docs/adr/0001.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import select
import shutil
import struct
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

import display_profile
from display_profile import ROTATIONS, parse_profile, profile_to_config

CONFIG_PATH = "/etc/marquee-pi/config.json"
CMDLINE = "/boot/firmware/cmdline.txt"
CMDLINE_BACKUP_SUFFIX = ".marquee-pi-before-display"
# Output names as the X modesetting driver prints them, by DRM connector type.
X_KIND_NAMES = {
    "VGA": "VGA", "DVI-I": "DVI", "DVI-D": "DVI", "DVI-A": "DVI", "Composite": "Composite", "SVIDEO": "TV",
    "LVDS": "LVDS", "Component": "CTV", "DIN": "DIN", "DP": "DP", "HDMI-A": "HDMI", "HDMI-B": "HDMI",
    "TV": "TV", "eDP": "eDP", "Virtual": "Virtual", "DSI": "DSI", "DPI": "DPI",
}
_CONNECTOR = re.compile(r"^card(\d+)-(.+)-(\d+)$")
_MODE = re.compile(r"^[1-9][0-9]{2,4}x[1-9][0-9]{2,4}$")
ABS_X, ABS_Y, ABS_MT_POSITION_X, ABS_MT_POSITION_Y = 0x00, 0x01, 0x35, 0x36
INPUT_PROP_DIRECT = 0x02  # the device sits on the screen, like a touchscreen
EV_KEY, EV_ABS, BTN_TOUCH = 0x01, 0x03, 0x14A
TEST_FLAG = "display-test"
BOOT_RECORD = "display-boot-changes.json"
CONFIRM_SECONDS = 15


# ---- detection ------------------------------------------------------------------------------------


@dataclass
class Connector:
    card: str            # "card1-HDMI-A-1"
    kind: str            # "HDMI-A"
    output: str          # the X output name, "HDMI-1"
    connected: bool
    modes: list = field(default_factory=list)  # "1920x1080", preferred first
    edid_name: str | None = None
    preferred: str | None = None
    size_cm: tuple | None = None

    @property
    def is_dsi(self) -> bool:
        return self.kind == "DSI"

    def title(self) -> str:
        label = self.edid_name or ("official-style DSI panel" if self.is_dsi else self.kind)
        size = self.preferred or (self.modes[0] if self.modes else "unknown size")
        return "%s  %s  %s" % (self.output, label, size)


@dataclass
class TouchDevice:
    name: str
    event: str | None    # "event0"
    bus: str
    is_touchscreen: bool


def parse_edid(data: bytes) -> dict | None:
    """Monitor name, preferred mode and physical size from a 128-byte EDID base block."""
    if len(data) < 128 or data[:8] != b"\x00\xff\xff\xff\xff\xff\xff\x00":
        return None
    result: dict = {"name": None, "preferred": None, "size_cm": None}
    manufacturer = (data[8] << 8) | data[9]
    letters = "".join(chr(((manufacturer >> shift) & 0x1F) + 64) for shift in (10, 5, 0))
    first_timing = True
    for offset in (54, 72, 90, 108):
        block = data[offset:offset + 18]
        if block[0] or block[1]:  # a detailed timing descriptor; the first one is the preferred mode
            if first_timing:
                width = block[2] | ((block[4] & 0xF0) << 4)
                height = block[5] | ((block[7] & 0xF0) << 4)
                if width and height:
                    result["preferred"] = "%dx%d" % (width, height)
            first_timing = False
        elif block[3] == 0xFC:  # monitor name
            text = block[5:18].split(b"\x0a")[0].decode("ascii", "replace").strip()
            if text:
                result["name"] = text
    if data[21] and data[22]:
        result["size_cm"] = (data[21], data[22])
    if not result["name"] and letters.isalpha():
        result["name"] = letters  # at least the manufacturer code
    return result


class System:
    """The running system; tests point `root` at a fixture tree."""

    def __init__(self, root: str = "/"):
        self.root = Path(root)

    def path(self, absolute: str) -> Path:
        return self.root / absolute.lstrip("/")

    def read(self, absolute: str) -> str | None:
        try:
            return self.path(absolute).read_text(encoding="utf-8", errors="replace")
        except OSError:
            return None

    @property
    def live(self) -> bool:
        return self.root == Path("/")


def detect_connectors(system: System) -> list[Connector]:
    drm = system.path("/sys/class/drm")
    found = []
    try:
        entries = sorted(drm.iterdir(), key=lambda entry: entry.name)
    except OSError:
        return found
    for entry in entries:
        match = _CONNECTOR.match(entry.name)
        if not match:
            continue
        kind, index = match.group(2), match.group(3)
        status = (system.read("/sys/class/drm/%s/status" % entry.name) or "").strip()
        modes, seen = [], set()
        for line in (system.read("/sys/class/drm/%s/modes" % entry.name) or "").splitlines():
            line = line.strip()
            if _MODE.match(line) and line not in seen:
                seen.add(line)
                modes.append(line)
        connector = Connector(entry.name, kind, "%s-%s" % (X_KIND_NAMES.get(kind, kind), index),
                              status == "connected", modes)
        try:
            edid = parse_edid((entry / "edid").read_bytes())
        except OSError:
            edid = None
        if edid:
            connector.edid_name, connector.preferred, connector.size_cm = edid["name"], edid["preferred"], edid["size_cm"]
        found.append(connector)
    # Two DRM cards can yield the same X output name; X shows one of them, so keep the connected one.
    unique: dict[str, Connector] = {}
    for connector in found:
        known = unique.get(connector.output)
        if known is None or (connector.connected and not known.connected):
            unique[connector.output] = connector
    return list(unique.values())


def _bitmap(words: str, word_bits: int) -> int:
    """`B: ABS=` style value: hexadecimal words, most significant first, without padding."""
    value = 0
    for word in words.split():
        value = (value << word_bits) | int(word, 16)
    return value


def detect_touch(system: System, word_bits: int | None = None) -> list[TouchDevice]:
    """Input devices from /proc/bus/input/devices; `is_touchscreen` marks the ones on a screen."""
    word_bits = word_bits or struct.calcsize("l") * 8
    text = system.read("/proc/bus/input/devices") or ""
    devices = []
    for block in re.split(r"\n\s*\n", text.strip()):
        name = event = bus = None
        props = absolute = 0
        for line in block.splitlines():
            if line.startswith("N: Name="):
                name = line.split("=", 1)[1].strip().strip('"')
            elif line.startswith("I: "):
                match = re.search(r"Bus=([0-9a-fA-F]+)", line)
                bus = {"0003": "USB", "0018": "I2C", "0019": "platform", "0000": "platform"}.get(
                    match.group(1).lower().zfill(4), "other") if match else None
            elif line.startswith("H: Handlers="):
                match = re.search(r"\b(event\d+)\b", line)
                event = match.group(1) if match else None
            elif line.startswith("B: PROP="):
                props = _bitmap(line.split("=", 1)[1], word_bits)
            elif line.startswith("B: ABS="):
                absolute = _bitmap(line.split("=", 1)[1], word_bits)
        if not name or not event:
            continue
        has = lambda bit: bool(absolute >> bit & 1)  # noqa: E731
        positions = (has(ABS_MT_POSITION_X) and has(ABS_MT_POSITION_Y)) or (has(ABS_X) and has(ABS_Y))
        devices.append(TouchDevice(name, event, bus or "other", bool(props & INPUT_PROP_DIRECT) and positions))
    return devices


def detect_backlights(system: System) -> list[str]:
    root = system.path("/sys/class/backlight")
    names = []
    try:
        for entry in sorted(root.iterdir(), key=lambda item: item.name):
            if (entry / "brightness").is_file() and (entry / "max_brightness").is_file():
                names.append(entry.name)
    except OSError:
        pass
    return names


def detect_model(system: System) -> str | None:
    text = system.read("/proc/device-tree/model")
    return text.replace("\x00", "").strip() if text else None


def choose_default(connectors: list[Connector]) -> Connector | None:
    connected = [c for c in connectors if c.connected]
    for kind in ("DSI", "HDMI-A", "HDMI-B"):
        for connector in connected:
            if connector.kind == kind:
                return connector
    return connected[0] if connected else None


def detection_report(system: System) -> dict:
    return {
        "model": detect_model(system),
        "connectors": [
            {"output": c.output, "card": c.card, "kind": c.kind, "connected": c.connected, "name": c.edid_name,
             "preferred": c.preferred, "modes": c.modes, "size_cm": c.size_cm} for c in detect_connectors(system)],
        "touch": [{"name": t.name, "event": t.event, "bus": t.bus, "touchscreen": t.is_touchscreen}
                  for t in detect_touch(system)],
        "backlights": detect_backlights(system),
    }


# ---- configuration files ----------------------------------------------------------------------------


def load_config(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def write_atomically(path: Path, text: str) -> None:
    """Replace a file keeping its owner and permissions (config.json is root:marqueepi 0640)."""
    pending = path.with_name(path.name + ".marquee-pi-pending")
    pending.write_text(text, encoding="utf-8")
    try:
        info = path.stat()
        os.chmod(pending, info.st_mode & 0o7777)
        os.chown(pending, info.st_uid, info.st_gid)
    except (OSError, AttributeError):
        pass
    os.replace(pending, path)


def save_profile(config_path: Path, profile: dict) -> None:
    config = load_config(config_path)
    config["display"] = profile_to_config(profile)
    write_atomically(config_path, json.dumps(config, indent=2) + "\n")


# ---- boot configuration -------------------------------------------------------------------------------


@dataclass
class BootPlan:
    file: str
    add: list
    why: list

    def empty(self) -> bool:
        return not self.add


def plan_boot_changes(connector: Connector, mode: str, cmdline: str, connectors: list[Connector]) -> BootPlan:
    """Stage 1: only a DSI screen needs a video= entry (HDMI in automatic mode needs nothing)."""
    tokens = cmdline.split()
    add, why = [], []
    if connector.is_dsi:
        wanted = mode if mode != "auto" else (connector.preferred or (connector.modes[0] if connector.modes else None))
        if wanted and not any(t.startswith("video=%s:" % connector.output) for t in tokens):
            add.append("video=%s:%s@60" % (connector.output, wanted))
            why.append("%s needs its mode fixed at boot or the kernel may not bring it up." % connector.output)
        for other in connectors:
            if other.kind == "Composite" and not any(t.startswith("video=%s:" % other.output) for t in tokens):
                add.append("video=%s:d" % other.output)
                why.append("%s is switched off so that X uses the screen you chose." % other.output)
    return BootPlan(CMDLINE, add, why)


def apply_boot_plan(system: System, plan: BootPlan, data_dir: Path) -> None:
    path = system.path(plan.file)
    original = path.read_text(encoding="utf-8")
    tokens = original.split()
    if "root=" not in original:
        raise SystemExit("cmdline.txt has no root= entry; not touching it")
    backup = path.with_name(path.name + CMDLINE_BACKUP_SUFFIX)
    if not backup.exists():
        shutil.copy2(path, backup)
    added = [token for token in plan.add if token not in tokens]
    write_atomically(path, " ".join(tokens + added) + "\n")
    record = data_dir / BOOT_RECORD
    previous = []
    try:
        previous = json.loads(record.read_text(encoding="utf-8")).get("added", [])
    except (OSError, ValueError):
        pass
    record.write_text(json.dumps({"file": plan.file, "added": sorted(set(previous + added)),
                                  "backup": backup.name}, indent=2) + "\n", encoding="utf-8")


def revert_boot_changes(system: System, data_dir: Path, say=print) -> int:
    record = data_dir / BOOT_RECORD
    try:
        data = json.loads(record.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        say("No boot configuration changes from marquee-pi-configure-display are recorded.")
        return 0
    path = system.path(data["file"])
    tokens = path.read_text(encoding="utf-8").split()
    kept = [token for token in tokens if token not in data.get("added", [])]
    write_atomically(path, " ".join(kept) + "\n")
    record.unlink()
    say("Removed from %s: %s" % (data["file"], " ".join(data.get("added", [])) or "nothing"))
    say("Reboot for this to take effect.")
    return 0


def write_instructions(plan: BootPlan, target: Path, profile: dict) -> None:
    lines = ["Marquee-Pi display setup: changes to make by hand", "",
             "Display profile chosen: output %s, mode %s, rotation %s, fit %s, touch %s" % (
                 profile["output"] or "automatic", profile["mode"], profile["rotation"], profile["fit"],
                 profile["touch_device"] or ("yes" if profile["touch"] else "none (view-only)")), ""]
    if plan.empty():
        lines.append("No change to the boot configuration is needed.")
    else:
        lines += ["1. Make a copy of %s." % plan.file,
                  "2. Open %s as root. It must stay ONE line." % plan.file,
                  "3. Add these entries at the end of that line, separated by spaces:", ""]
        lines += ["       " + token for token in plan.add]
        lines += ["", "Why:"] + ["  - " + reason for reason in plan.why]
        lines += ["", "4. Save the file and reboot.",
                  "If the screen stays black afterwards, put the copy back (a card reader on another computer works)."]
    target.write_text("\n".join(lines) + "\n", encoding="utf-8")


# ---- talking to the user ----------------------------------------------------------------------------------


class Prompter:
    def say(self, text: str = "") -> None:
        print(text, flush=True)

    def ask(self, question: str, default: str | None = None, choices: list | None = None) -> str:
        suffix = " [%s]" % default if default is not None else ""
        while True:
            answer = input("%s%s: " % (question, suffix)).strip()
            if not answer and default is not None:
                return default
            if choices is None or answer in choices:
                return answer
            print("Please answer with one of: %s" % ", ".join(choices))

    def confirm(self, question: str, default: bool = True) -> bool:
        answer = self.ask(question + " (y/n)", "y" if default else "n", ["y", "n", "Y", "N"])
        return answer.lower() == "y"

    def ask_timeout(self, question: str, seconds: int) -> str | None:
        print("%s (%d s): " % (question, seconds), end="", flush=True)
        ready, _, _ = select.select([sys.stdin], [], [], seconds)
        if not ready:
            print()
            return None
        return sys.stdin.readline().strip()


def identify_touch(system: System, candidates: list[TouchDevice], seconds: int = 20) -> TouchDevice | None:
    """Wait for a touch on the screen and return the device that reported it."""
    size = struct.calcsize("llHHi")
    handles = {}
    try:
        for device in candidates:
            try:
                handles[os.open(str(system.path("/dev/input/" + device.event)), os.O_RDONLY | getattr(os, "O_NONBLOCK", 0))] = device
            except OSError:
                continue
        deadline = time.monotonic() + seconds
        while handles:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            ready, _, _ = select.select(list(handles), [], [], remaining)
            for fd in ready:
                try:
                    data = os.read(fd, size * 16)
                except OSError:
                    continue
                for offset in range(0, len(data) - size + 1, size):
                    _, _, kind, code, value = struct.unpack("llHHi", data[offset:offset + size])
                    if (kind == EV_KEY and code == BTN_TOUCH and value == 1) or kind == EV_ABS:
                        return handles[fd]
    finally:
        for fd in handles:
            os.close(fd)
    return None


# ---- the profile ---------------------------------------------------------------------------------------------


def build_profile(connector: Connector | None, touch: TouchDevice | None, backlight: str | None,
                  mode: str = "auto", rotation: int = 0, fit: str = "contain", scale="auto") -> dict:
    profile = display_profile.default_profile()
    profile.update({"configured": True, "output": connector.output if connector else None, "mode": mode,
                    "rotation": rotation, "fit": fit, "scale": scale, "touch": touch is not None,
                    "touch_device": touch.name if touch else None,
                    "backlight": backlight if connector and connector.is_dsi else None})
    return profile


def dropin_path(system: System) -> Path:
    return system.path("/etc/systemd/system/marquee-pi-api.service.d/20-backlight.conf")


def write_backlight_dropin(system: System, profile: dict) -> bool:
    """Let the API service write the chosen backlight (it runs with ProtectSystem=strict)."""
    target = dropin_path(system)
    name = profile.get("backlight")
    if not name:
        if target.exists():
            target.unlink()
            return True
        return False
    real = os.path.realpath(system.path("/sys/class/backlight/" + name))
    device = "/" + os.path.relpath(real, system.root).replace(os.sep, "/") if not system.live else real
    text = "[Service]\nReadWritePaths=-%s\n" % device
    if target.exists() and target.read_text(encoding="utf-8") == text:
        return False
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")
    return True


def summary(profile: dict) -> list[str]:
    return [
        "  screen:   %s" % (profile["output"] or "automatic (X decides)"),
        "  mode:     %s" % profile["mode"],
        "  rotation: %d degrees" % profile["rotation"],
        "  fit:      %s" % profile["fit"],
        "  scale:    %s" % profile["scale"],
        "  touch:    %s" % ((profile["touch_device"] or "yes") if profile["touch"] else "none - view-only display"),
        "  backlight: %s" % (profile["backlight"] or "none"),
    ]


@dataclass
class Context:
    system: System
    config_path: Path
    prompter: Prompter
    dry_run: bool = False
    restart: bool = True
    test_picture: bool = True
    instructions: Path | None = None
    run: object = None  # callable(list[str]) -> (returncode, output); replaced in tests
    identify: object = None

    def data_dir(self) -> Path:
        value = load_config(self.config_path).get("data_dir") or "/var/lib/marquee-pi"
        return self.system.path(value) if not self.system.live else Path(value)

    def command(self, arguments: list[str]) -> tuple[int, str]:
        if self.run:
            return self.run(arguments)
        try:
            result = subprocess.run(arguments, capture_output=True, text=True, timeout=60, check=False)
        except (OSError, subprocess.TimeoutExpired) as exc:
            return 1, str(exc)
        return result.returncode, result.stdout + result.stderr

    def systemd(self) -> bool:
        return self.system.live and shutil.which("systemctl") is not None and Path("/run/systemd/system").exists()


def restart_services(ctx: Context, dropin_changed: bool, say) -> None:
    if not ctx.restart or not ctx.systemd():
        return
    if dropin_changed:
        ctx.command(["systemctl", "daemon-reload"])
    ctx.command(["systemctl", "restart", "marquee-pi-api", "marquee-pi-kiosk"])
    say("Restarted the Marquee-Pi services.")


def test_and_confirm(ctx: Context, old_config: dict, profile: dict, say) -> bool:
    """Show the test picture with the new profile; put the old profile back unless confirmed."""
    if not ctx.test_picture or not ctx.systemd() or ctx.command(["systemctl", "is-active", "--quiet", "marquee-pi-kiosk"])[0] != 0:
        say("(No test picture: the kiosk is not running here.)")
        return True
    flag = ctx.data_dir() / TEST_FLAG
    save_profile(ctx.config_path, profile)
    flag.touch()
    ctx.command(["systemctl", "restart", "marquee-pi-api", "marquee-pi-kiosk"])
    say("A test picture is on the screen now: a cyan arrow for 'up', the output name and the resolution.")
    answer = ctx.prompter.ask_timeout("Does it look right? y = keep, n = go back", CONFIRM_SECONDS)
    keep = bool(answer) and answer.lower().startswith("y")
    if flag.exists():
        flag.unlink()
    if not keep:
        write_atomically(ctx.config_path, json.dumps(old_config, indent=2) + "\n")
        ctx.command(["systemctl", "restart", "marquee-pi-api", "marquee-pi-kiosk"])
        say("Put the previous settings back." if answer is not None else "No answer: put the previous settings back.")
    return keep


# ---- the two ways in ---------------------------------------------------------------------------------------------


def run_auto(ctx: Context, force: bool, say=print) -> int:
    config = load_config(ctx.config_path)
    connectors = detect_connectors(ctx.system)
    backlights = detect_backlights(ctx.system)
    if config.get("display") is not None and not force:
        say("Display profile already set; keeping it.")
        profile, _ = parse_profile(config["display"])
        if not ctx.dry_run:
            write_backlight_dropin(ctx.system, profile)
        return 0
    connector = choose_default(connectors)
    touch_devices = [t for t in detect_touch(ctx.system) if t.is_touchscreen]
    profile = build_profile(connector, touch_devices[0] if touch_devices else None,
                            backlights[0] if backlights else None)
    say("Display profile (automatic, no changes to the boot configuration):")
    for line in summary(profile):
        say(line)
    if ctx.dry_run:
        say("(dry run: nothing written)")
        return 0
    save_profile(ctx.config_path, profile)
    write_backlight_dropin(ctx.system, profile)
    say("Saved. Run 'sudo marquee-pi-configure-display' to choose another screen.")
    return 0


def run_interactive(ctx: Context) -> int:
    ask, say = ctx.prompter, ctx.prompter.say
    connectors = detect_connectors(ctx.system)
    touch_devices = [t for t in detect_touch(ctx.system) if t.is_touchscreen]
    backlights = detect_backlights(ctx.system)
    model = detect_model(ctx.system)
    say("Marquee-Pi display setup" + (" on a %s" % model if model else ""))
    say()
    connected = [c for c in connectors if c.connected]
    if not connected:
        say("No connected screen was found. Connect the display, switch it on and run this command again.")
        return 1
    say("Screens found:")
    for number, connector in enumerate(connected, 1):
        say("  %d) %s" % (number, connector.title()))
    default = choose_default(connectors)
    choice = ask.ask("Which screen should show the marquee?", str(connected.index(default) + 1),
                     [str(n) for n in range(1, len(connected) + 1)])
    connector = connected[int(choice) - 1]
    others = [c for c in connected if c is not connector]
    if others:
        say("The other screen(s) will be switched off: %s" % ", ".join(c.output for c in others))

    mode = "auto"
    if len(connector.modes) > 1:
        listing = ", ".join(connector.modes[:8])
        answer = ask.ask("Mode (auto = preferred %s; other choices: %s)" % (connector.preferred or connector.modes[0], listing),
                         "auto")
        mode = answer if answer == "auto" or answer in connector.modes else "auto"
    rotation = int(ask.ask("Rotation in degrees (0, 90, 180, 270)", "0", [str(r) for r in ROTATIONS]))
    fit = ask.ask("Picture fit: contain (whole picture, bars if needed) or cover (fills the screen, crops)", "contain",
                  ["contain", "cover"])

    touch = None
    if touch_devices:
        if len(touch_devices) == 1:
            if ask.confirm("Touch device found: %s (%s). Use it on this screen?" % (touch_devices[0].name, touch_devices[0].bus)):
                touch = touch_devices[0]
        else:
            say("Several touch devices found:")
            for number, device in enumerate(touch_devices, 1):
                say("  %d) %s (%s)" % (number, device.name, device.bus))
            say("  0) none - a view-only display")
            say("Touch the screen now to pick the right one, or type its number.")
            tapped = (ctx.identify or (lambda devices: identify_touch(ctx.system, devices)))(touch_devices)
            if tapped:
                say("Detected: %s" % tapped.name)
                touch = tapped
            else:
                number = int(ask.ask("Number", "0", [str(n) for n in range(0, len(touch_devices) + 1)]))
                touch = touch_devices[number - 1] if number else None
    else:
        say("No touch device found: this will be a view-only display (the marquee only, controlled from Windows).")

    profile = build_profile(connector, touch, backlights[0] if backlights else None, mode, rotation, fit)
    say()
    say("Profile:")
    for line in summary(profile):
        say(line)

    plan = plan_boot_changes(connector, mode, ctx.system.read(CMDLINE) or "", connectors) if ctx.system.read(CMDLINE) else BootPlan(CMDLINE, [], [])
    apply_boot = False
    if not plan.empty():
        say()
        say("This screen needs a change to the boot configuration (%s):" % plan.file)
        for token in plan.add:
            say("  + " + token)
        for reason in plan.why:
            say("    " + reason)
        say("  1) Apply it now (a backup is kept; 'marquee-pi-configure-display --revert' undoes it)")
        say("  2) Write the changes to a text file and do them by hand")
        say("  3) Skip it")
        what = ask.ask("Choice", "1", ["1", "2", "3"])
        if what == "2":
            target = ctx.instructions or Path.cwd() / "marquee-pi-display-changes.txt"
            if not ctx.dry_run:
                write_instructions(plan, target, profile)
            say("Wrote %s" % target)
        apply_boot = what == "1"
    if not ask.confirm("Save this profile?"):
        say("Nothing was changed.")
        return 0
    if ctx.dry_run:
        say("(dry run: nothing written)")
        return 0

    old_config = load_config(ctx.config_path)
    confirmed = test_and_confirm(ctx, old_config, profile, say)
    if not confirmed:
        return 1
    save_profile(ctx.config_path, profile)
    changed = write_backlight_dropin(ctx.system, profile)
    if apply_boot:
        apply_boot_plan(ctx.system, plan, ctx.data_dir())
        say("Boot configuration changed. Reboot for it to take effect.")
    restart_services(ctx, changed, say)
    say("Done.")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="marquee-pi-configure-display", description=__doc__.split("\n\n")[0])
    parser.add_argument("--config", default=CONFIG_PATH)
    parser.add_argument("--root", default="/", help="read /sys and /proc below this directory (for tests)")
    parser.add_argument("--auto", action="store_true", help="no questions; keep an existing profile")
    parser.add_argument("--force", action="store_true", help="with --auto: replace an existing profile")
    parser.add_argument("--dry-run", action="store_true", help="show what would happen, write nothing")
    parser.add_argument("--json", action="store_true", help="print the detection result and exit")
    parser.add_argument("--revert", action="store_true", help="undo boot configuration changes made by this tool")
    parser.add_argument("--instructions", type=Path, help="where to write the manual steps (option 2)")
    parser.add_argument("--no-test", action="store_true", help="do not show the test picture")
    parser.add_argument("--no-restart", action="store_true", help="do not restart the services (used by the package)")
    args = parser.parse_args(argv)

    system = System(args.root)
    if args.json:
        print(json.dumps(detection_report(system), indent=2))
        return 0
    if not args.dry_run and system.live and os.geteuid() != 0:
        print("Please run this with sudo: it changes %s." % args.config, file=sys.stderr)
        return 1
    ctx = Context(system, Path(args.config), Prompter(), dry_run=args.dry_run, restart=not args.no_restart,
                  test_picture=not args.no_test, instructions=args.instructions)
    if args.revert:
        return revert_boot_changes(system, ctx.data_dir())
    if args.auto:
        return run_auto(ctx, args.force)
    return run_interactive(ctx)


if __name__ == "__main__":
    sys.exit(main())
