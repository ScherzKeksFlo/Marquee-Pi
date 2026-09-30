#!/usr/bin/env python3
"""The display profile: which screen Marquee-Pi uses and how.

It lives in the "display" section of /etc/marquee-pi/config.json and is read by the API
server, by marquee-pi-configure-display and, through `--env`, by start-kiosk.sh.

    "display": {
      "output": "DSI-1",            X output name; null lets X pick
      "mode": "auto",              "auto" or "WIDTHxHEIGHT"
      "rotation": 0,               0, 90, 180 or 270 degrees clockwise
      "fit": "contain",            "contain" (letterbox) or "cover" (crop)
      "scale": "auto",             "auto" (short side = 480 CSS px) or a number 0.5-4
      "touch": {"device": "..."},  null or false for a view-only display
      "backlight": "rpi_backlight" name below /sys/class/backlight, optional
    }

Parsing is lenient: wrong values fall back to the defaults and are reported as warnings, so a
typo in the file never keeps the API from starting.
"""
from __future__ import annotations

import json
import re
import shlex
import sys

ROTATIONS = (0, 90, 180, 270)
FITS = ("contain", "cover")
# Rotation in degrees clockwise -> xrandr name and the libinput coordinate transformation matrix
# that keeps touch aligned with the rotated picture.
XRANDR_ROTATE = {0: "normal", 90: "right", 180: "inverted", 270: "left"}
TOUCH_MATRIX = {
    0: "1 0 0 0 1 0 0 0 1",
    90: "0 1 0 -1 0 1 0 0 1",
    180: "-1 0 1 0 -1 1 0 0 1",
    270: "0 -1 1 1 0 0 0 0 1",
}
MIN_SCALE, MAX_SCALE = 0.5, 4.0
REFERENCE_SHORT_SIDE = 480  # the layout is designed for a short side of 480 CSS px

_MODE = re.compile(r"^[1-9][0-9]{2,4}x[1-9][0-9]{2,4}$")
_NAME = re.compile(r"^[^\x00-\x1f]{1,120}$")


def default_profile() -> dict:
    return {"output": None, "mode": "auto", "rotation": 0, "fit": "contain", "scale": "auto",
            "touch": True, "touch_device": None, "backlight": None, "configured": False}


def _text(value, limit: int = 120) -> str | None:
    if isinstance(value, str) and _NAME.match(value.strip()[:limit] or "\x00"):
        return value.strip()[:limit]
    return None


def parse_profile(raw) -> tuple[dict, list[str]]:
    """Return (profile, warnings) for the value of the "display" key (None when absent)."""
    profile = default_profile()
    warnings: list[str] = []
    if raw is None:
        return profile, warnings  # old configuration: touch is assumed, as before
    if not isinstance(raw, dict):
        return profile, ['"display" must be an object; using defaults']
    profile["configured"] = True

    if raw.get("output") is not None:
        output = _text(raw["output"])
        if output and re.match(r"^[A-Za-z0-9._+-]+$", output):
            profile["output"] = output
        else:
            warnings.append('"display.output" is not a valid output name; ignored')

    mode = raw.get("mode", "auto")
    if mode == "auto" or (isinstance(mode, str) and _MODE.match(mode)):
        profile["mode"] = mode
    else:
        warnings.append('"display.mode" must be "auto" or "WIDTHxHEIGHT"; using auto')

    rotation = raw.get("rotation", 0)
    if isinstance(rotation, int) and not isinstance(rotation, bool) and rotation in ROTATIONS:
        profile["rotation"] = rotation
    else:
        warnings.append('"display.rotation" must be 0, 90, 180 or 270; using 0')

    fit = raw.get("fit", "contain")
    if fit in FITS:
        profile["fit"] = fit
    else:
        warnings.append('"display.fit" must be "contain" or "cover"; using contain')

    scale = raw.get("scale", "auto")
    if scale == "auto":
        profile["scale"] = "auto"
    elif isinstance(scale, (int, float)) and not isinstance(scale, bool) and MIN_SCALE <= scale <= MAX_SCALE:
        profile["scale"] = float(scale)
    else:
        warnings.append('"display.scale" must be "auto" or a number from 0.5 to 4; using auto')

    if "touch" in raw:
        touch = raw["touch"]
        if isinstance(touch, dict):
            profile["touch"] = True
            profile["touch_device"] = _text(touch.get("device"))
        elif touch in (None, False):
            profile["touch"] = False
        else:
            warnings.append('"display.touch" must be an object or null; touch is assumed')
    # no "touch" key: touch is assumed, like in configurations from before display profiles

    if raw.get("backlight") is not None:
        backlight = _text(raw["backlight"])
        if backlight and "/" not in backlight:
            profile["backlight"] = backlight
        else:
            warnings.append('"display.backlight" must be a device name; ignored')
    return profile, warnings


def profile_to_config(profile: dict) -> dict:
    """The JSON form of a parsed profile, as written to config.json."""
    touch = None
    if profile.get("touch"):
        touch = {"device": profile.get("touch_device")}
    result = {
        "output": profile.get("output"),
        "mode": profile.get("mode", "auto"),
        "rotation": profile.get("rotation", 0),
        "fit": profile.get("fit", "contain"),
        "scale": profile.get("scale", "auto"),
        "touch": touch,
    }
    if profile.get("backlight"):
        result["backlight"] = profile["backlight"]
    return result


def auto_scale(width: int, height: int) -> float:
    """Scale that makes the short side 480 CSS px, rounded to 0.05 and limited to 0.5-4."""
    short = max(1, min(width, height))
    return max(MIN_SCALE, min(MAX_SCALE, round(short / REFERENCE_SHORT_SIDE / 0.05) * 0.05))


def shell_env(profile: dict) -> str:
    """KEY='value' lines for `. file` in start-kiosk.sh."""
    scale = profile["scale"]
    values = {
        "MP_CONFIGURED": "1" if profile["configured"] else "0",
        "MP_OUTPUT": profile["output"] or "",
        "MP_MODE": "" if profile["mode"] == "auto" else profile["mode"],
        "MP_ROTATE": XRANDR_ROTATE[profile["rotation"]],
        "MP_MATRIX": TOUCH_MATRIX[profile["rotation"]],
        "MP_SCALE": "auto" if scale == "auto" else ("%g" % scale),
        "MP_TOUCH_DEVICE": (profile["touch_device"] or "") if profile["touch"] else "",
    }
    return "".join("%s=%s\n" % (key, shlex.quote(value)) for key, value in values.items())


def main(argv: list[str]) -> int:
    if len(argv) == 3 and argv[1] == "--env":
        try:
            with open(argv[2], encoding="utf-8") as handle:
                raw = json.load(handle).get("display")
        except (OSError, ValueError, AttributeError):
            raw = None
        profile, warnings = parse_profile(raw)
        for warning in warnings:
            print(warning, file=sys.stderr)
        sys.stdout.write(shell_env(profile))
        return 0
    if len(argv) == 4 and argv[1] == "--scale":
        try:
            print("%g" % auto_scale(int(argv[2]), int(argv[3])))
        except ValueError:
            return 2
        return 0
    print("usage: display_profile.py --env CONFIG.json | --scale WIDTH HEIGHT", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
