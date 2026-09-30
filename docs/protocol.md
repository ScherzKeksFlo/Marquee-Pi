# Network protocol v1

The Pi provides an HTTP API on the configured port. Windows sends the `X-Marquee-Token` header with every `/v1` call. HTTP encrypts neither media nor the token; a device on the same network could read the token. The API is therefore intended exclusively for a direct point-to-point connection or a trusted local network and must not be forwarded to the internet.

`allowed_client_ips` restricts the permitted Windows addresses; an empty list means no IP filtering. A list containing the fixed IP of the arcade PC is recommended. Alternatively or additionally, `bind` is set to the Pi IP of the direct Ethernet interface so that the service does not listen on Wi-Fi or other networks. Independently of this, the full-screen page at `/ui/` is reachable only from `127.0.0.1` or `::1`.

## Operations

| Call | Purpose |
| --- | --- |
| `GET /v1/status` | Read program version, game and active default media |
| `POST /v1/game` | Set game and artwork |
| `POST /v1/heartbeat` | Confirm the running game while the Windows connection is up |
| `POST /v1/default` | End the game and show the default media |
| `POST /v1/default-media` | Upload default media and activate it after verification |
| `POST /v1/boot-splash` | Upload a static boot image and activate it permanently |
| `POST /v1/shutdown-media` | Upload a shutdown image or video and activate it permanently |
| `POST /v1/gesture-config` | Save the assignment of the four swipes and the long press |
| `POST /v1/language` | Set the language of the touch menu (`en` or `de`) |
| `GET /v1/gesture-events?after=N` | Retrieve gesture events for Windows by sequential ID |
| `POST /v1/reload` | Reload the active media and refresh the browser display |
| `POST /v1/reboot` | Request a Pi restart |
| `POST /v1/shutdown` | Request a Pi shutdown |

`POST /v1/game` uses JSON. `title` is required. `core` (libretro core name, at most 120 characters) and `rom` (ROM file name, at most 200) are optional strings for emulated games; other types are ignored. Both appear as `game_core` and `game_rom` in `/v1/status` and `/ui/state` and are cleared with the game. `marquee`, `controls`, `box_art` and `logo` are optional and each contain `extension` and `base64`. File paths are not transmitted because Windows paths are not available on the Pi. Images are held in RAM for the running session. PNG and JPEG files are scaled down proportionally on Windows to a maximum edge length of 1600 pixels before transmission. GIF and WebP are transmitted unchanged so that animations are preserved. If no heartbeat arrives for 60 seconds, the Pi returns to the local default media. The timeout is configurable.

The maximum HTTP request size is 32 MiB. The Windows sender reserves 1 MiB of this for JSON, title and Base64 rounding. The shared raw data budget for all four artworks is therefore `(32 MiB - 1 MiB) * 3 / 4 = 23.25 MiB`. Changes to this protocol limit must be made in `pi/marquee_pi.py` and `windows/native/core.hpp` together.

`POST /v1/gesture-config` uses a JSON object with the gesture keys `swipe-down`, `swipe-up`, `swipe-right`, `swipe-left` and `long-press`. Valid actions are `none`, `marquee`, `box_art`, `logo`, `controls`, `default`, `retroarch_menu` and `touch_menu`. Missing swipe keys are treated as `none`, a missing `long-press` as `touch_menu` so that clients that only know the swipes keep the menu; invalid keys or values are rejected. The assignment is stored permanently on the Pi. The local page reports only RetroArch gestures via `POST /ui/gesture`; image changes and the touch menu are handled directly in the browser. Windows polls `/v1/gesture-events?after=N` with authentication and receives `instance_id` and a list of IDs. When the Pi restarts, `instance_id` changes so that Windows resets its cursor.

`POST /v1/language` takes `{"language": "en"}` or `{"language": "de"}`, stores it permanently and returns it as `language` in `/v1/status` and the local display state. The default is `en`. The Windows app sends it together with the gesture configuration, resolved from its own setting (which may follow the Windows display language).

The touch menu uses these endpoints, which like all `/ui/` paths are reachable only from `127.0.0.1` or `::1` and need no token: `GET /ui/system` (addresses, connection of the Windows app, brightness, whether power commands are enabled), `POST /ui/brightness` with `{"percent": n}` (clamped to 5–100) and `POST /ui/power` with `{"action": "reboot"}` or `{"action": "poweroff"}`. Power requests are checked with polkit exactly like `/v1/reboot` and `/v1/shutdown`.

`POST /v1/default-media` uses the raw bytes of the file. `X-File-Name` provides the extension. The API checks the type and size limit (currently 20 MiB); for MP4, `ffprobe` must confirm an H.264 video stream. The file is activated permanently only after complete verification. A failed upload leaves the previous media active.

`POST /v1/boot-splash` uses the same raw data format but accepts only PNG or JPEG. `POST /v1/shutdown-media` accepts the supported image and animation formats as well as H.264 MP4. On shutdown, the local display switches to this media first. For a video, the Pi waits for its detected duration plus a short margin, capped at 30 seconds, before running `systemctl poweroff`.

Successful changes return JSON with `ok: true`. Errors return an HTTP status and a JSON `error`. Restart and shutdown acknowledge the accepted command before the Pi connection ends.

## Reconnection

The Windows tool remembers the running game and sends it again after the Pi connection is restored. Without a connection, the Pi shows its locally stored default media. A network loss does not trigger a Pi shutdown, so that Windows can be restarted.
