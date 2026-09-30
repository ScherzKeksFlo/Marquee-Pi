# Displays

Marquee-Pi shows the marquee on one screen attached to the Raspberry Pi. Which screen and how is the **display profile**.

## What is supported

| Kind | Status | Notes |
| --- | --- | --- |
| DSI, official 7" touch display (800 × 480) on a Pi 3 B+ | **tested** | The hardware the project was built on. |
| HDMI monitors and panels, any resolution, landscape or portrait | untested | Detected through the DRM connector and its EDID. Wide bar displays work through `fit` and `scale`. |
| Touch on HDMI panels (USB or I²C touch controllers) | untested | Matched to the screen by `marquee-pi-configure-display`; rotation applies a coordinate matrix with `xinput`. |
| View-only displays (no touch device) | untested | The page ignores all input. |
| GPIO / SPI displays, USB display adapters (DisplayLink) | not supported | Planned for a later stage at the earliest. |

"Untested" means the logic is covered by tests against recorded detection data and a stubbed `xrandr`, `xinput` and browser, but not yet run on that hardware. Please report what you tried.

## The profile

`marquee-pi-configure-display` writes it; you can also edit `/etc/marquee-pi/config.json` by hand and restart `marquee-pi-api` and `marquee-pi-kiosk`.

```json
"display": {
  "output": "HDMI-1",
  "mode": "auto",
  "rotation": 0,
  "fit": "contain",
  "scale": "auto",
  "touch": {"device": "ILITEK ILITEK-TP"},
  "backlight": "rpi_backlight"
}
```

| Key | Values | Meaning |
| --- | --- | --- |
| `output` | X output name such as `HDMI-1`, `DSI-1`; `null` | The one screen that is used. The other connected screens are switched off at kiosk start. `null` leaves the choice to X. |
| `mode` | `"auto"` or `"1920x1080"` | `auto` is the preferred mode of the screen. |
| `rotation` | `0`, `90`, `180`, `270` | Degrees clockwise. Touch is rotated with the picture. |
| `fit` | `"contain"`, `"cover"` | `contain` shows the whole picture with bars; `cover` fills the screen and crops. |
| `scale` | `"auto"` or a number from 0.5 to 4 | `auto` makes the short side of the screen 480 CSS pixels; this scales the touch menu and the gesture distances. |
| `touch` | `{"device": "name"}`, `null` | The touch device of this screen as `xinput` names it. `null` (or `false`) makes a view-only display. |
| `backlight` | name below `/sys/class/backlight` | Optional; used for the brightness control. The command also lets the API service write it. |

An installation without a `display` section keeps behaving as before: touch is assumed, nothing is rotated or scaled, and the DSI and Composite outputs are handled as in earlier versions. Wrong values fall back to the defaults and are logged when the API starts.

## How detection works

The command reads `/sys/class/drm/card*-*` (connector status, modes, EDID for the monitor name and preferred mode) and `/proc/bus/input/devices` (a device counts as a touchscreen if it reports absolute positions and is marked as sitting directly on the screen, which mice and touchpads are not). It needs no running X server. `marquee-pi-configure-display --json` prints the result, which is also what to attach to a bug report.

The kiosk applies the profile at every start: `xrandr` for the output, mode and rotation, `xinput` for the touch matrix, and Chromium's `--force-device-scale-factor` for the scale. It gets the profile from the local API (`GET /ui/display-profile`), because `config.json` is not readable for the kiosk user.

## What the Windows app sees

`GET /v1/status` contains a `display` block with `output`, `width` and `height` (physical pixels, reported by the kiosk page), `rotation`, `fit`, `scale`, `touch` and `pi_model`. The Windows app uses it for the aspect ratio of previews and thumbnails, the texts that mention the resolution, the note on the gestures page and the size limit of artwork. Older Pis send no block; Windows then assumes 800 × 480 with touch.
