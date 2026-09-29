# Pi display program

The complete guide for a fresh Raspberry Pi OS Lite installation is in [INSTALL.md](INSTALL.md). The Trixie installation and display were tested on a Pi 3 B+ with a 7-inch DSI display. For a black DSI picture on kernel 6.18, INSTALL.md describes the tested FKMS workaround.

The Python server provides the local full-screen display and a token-protected API. The display loads a default image or video stored on the Pi, shows game artwork and handles taps as well as four swipe directions. The boot splash and shutdown media are also uploaded via the Windows tool and stored permanently on the Pi.

## Touch menu

An overlay menu directly on the display, opened by default with a long press (approx. 0.8 s, hold the finger still). It offers the views Marquee, Box Art, Logo, Controls and Default, the brightness in 10% steps (minimum 5%, retained after a restart) and a status display (connection to the arcade PC, IP addresses, game, version). Restart and shut down are on a separate page behind the **System …** button, with confirmation. Which gesture opens the menu is determined by the `touch_menu` action: it can be assigned to any of the five gestures `long-press`, `swipe-down`, `swipe-up`, `swipe-right` and `swipe-left` (Windows tool, **Gestures** tab, or `POST /v1/gesture-config`). Without configuration, `long-press` stays on `touch_menu`; if `long-press` is set to `none`, a long press triggers nothing. After 20 s without input, the menu closes by itself. The associated endpoints `/ui/system`, `/ui/brightness` and `/ui/power` are, like all `/ui/` paths, reachable only from `127.0.0.1`. Restart and shutdown use the same Polkit check as the API and require `power_commands_enabled`. For brightness, the service needs write access to `/sys/class/backlight/*/brightness`; the service template grants the `video` group and the path `/sys/devices/platform/rpi_backlight` for this.

## Requirements

- Raspberry Pi OS Lite with X11, `xinit`, `xset` (`x11-xserver-utils`) and Chromium (tested on the Pi 3 B+ with Trixie/Python 3.13)
- Python 3
- `ffprobe` from FFmpeg for MP4 uploads
- `fbi` for the static image before the X11 kiosk
- `curl` for the local shutdown display
- A permanently writable data folder for the default media
- Network connection to the Windows PC for game events; it is not required to start the default display

## Running locally

1. Copy `config.example.json` to `config.json` and set a random token of at least 24 characters. Do not add this file to Git.
2. Set `data_dir` to a permanently writable path and assign the folder to the Pi service user.
3. Start `python3 marquee_pi.py --config config.json`.
4. Open `http://127.0.0.1:8765/ui/` in the browser. `start-kiosk.sh` starts Chromium in full-screen mode.

`marquee-pi-api.service.example`, `marquee-pi-kiosk.service.example`, `marquee-pi-boot-splash.service.example` and `marquee-pi-shutdown-animation.service.example` are templates for system startup. User name and paths must match the Pi installation. The kiosk service starts Xorg on `tty7` and Chromium without a desktop session. `start-kiosk.sh` disables the screen saver and DPMS at X11 startup. The boot service shows the uploaded PNG/JPEG on `tty1`; the shutdown service shows the stored media while the API and kiosk are still running. The current Trixie installation uses the Chromium policy under `/etc/chromium/policies/managed/`.

## Restart and shutdown

The API commands are disabled by default and respond with HTTP 503. On Trixie, they run via `systemctl` and a Polkit rule for the dedicated service user `marqueepi`; `NoNewPrivileges=true` remains active.

For the service template, create the system user `marqueepi` without a login shell, assign `/var/lib/marquee-pi` to this user and store `/etc/marquee-pi/config.json` as `root:marqueepi` with mode `640`. The PKLA template belongs at `/etc/polkit-1/localauthority/50-local.d/marquee-pi.pkla` on Buster only. On Trixie, the JavaScript rule `marquee-pi.rules.example` under `/etc/polkit-1/rules.d/` applies. Then enable the four services described in INSTALL.md. Enable `power_commands_enabled` only after installing the rule, checking with `pkcheck` and a restart test. API restart and shutdown have been tested on the target device.

## Tests

From the repository root:

```text
PYTHONPATH=pi python3 -m unittest discover -s pi/tests -v
node pi/tests/gesture.test.js
```

The Windows PowerShell equivalent of the first command is `$env:PYTHONPATH='pi'; python -m unittest discover -s pi/tests -v`. Further details are in [docs/media.md](../docs/media.md) and [docs/touch.md](../docs/touch.md).

## Device test

Tested on a Raspberry Pi 3 B+ with Raspberry Pi OS Trixie, kernel 6.18, Chromium and an 800 × 480 touch display: API tests, touch gestures, H.264 default video, real Big Box game switches, RetroArch menu invocation as well as shutdown via Big Box and the Windows Start menu work. The DSI display was detected with `bootcode_delay=5` in three consecutive cold starts.
