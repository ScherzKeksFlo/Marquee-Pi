# Changelog

## 1.0.0-beta.2

Follows `1.0.0-beta.1`. It is tested on the same single setup (Raspberry Pi 3 B+ with the 7" DSI touch display on Raspberry Pi OS Trixie, Windows 11 arcade PC with LaunchBox/Big Box), not yet on others.

### Highlights

- **Redesigned Windows settings** in an arcade theme: sidebar (or tabs), a new **Dashboard** and a **Logs** page, a tray flyout instead of the menu, and matching dialogs. Dark or light theme and sidebar or tabs navigation via `Theme` and `Navigation` in `settings.ini`. The window is DPI aware.
- **Test connection** on the Connection page, media uploads with a progress bar and a retry banner, and **Language** that applies immediately.
- Fonts (Silkscreen, Chakra Petch, IBM Plex Mono, SIL OFL) are bundled in the EXE.
- **Variable displays on the Pi.** A display profile (screen, mode, rotation, picture fit, scale, touch device) in `/etc/marquee-pi/config.json`, set up by the new `marquee-pi-configure-display`: it detects the connected HDMI and DSI screens, asks which one to use, shows a test picture and saves the choice. The package runs it on a fresh install and derives a profile without questions otherwise. A display without touch only shows the marquee. Boot configuration changes (`cmdline.txt`) happen only after confirmation, or as a text file for manual use. The Windows app adapts previews and texts to the reported display. See `docs/displays.md`. Only the DSI display was tested on hardware; HDMI and the touch assignment were tested with recorded detection data.
- **Core and ROM in the touch menu.** The status block shows the libretro core and the ROM file name of the running game. It needs the updated LaunchBox plugin DLL, the Windows app and the Pi program; with older parts the two rows stay hidden.
- **Touch menu on the Pi in the same look** as the Windows app: arcade colors, the same fonts (served from the Pi, shipped in the Debian package), pink selection and glow, red danger buttons, a status dot for the connection. Layout and texts are unchanged.

### Changes

- Saving keeps the settings window open and confirms with a message.
- After the Pi was unreachable, the next status poll happens at once instead of after up to five seconds; the Windows tool no longer sends the running game twice when it first sees a Pi, and resends it when settings change during the send.
- The Windows tool's Pi traffic now lives in `PiSync` with a scripted fake Pi in the tests.

### Upgrading

- Update the Windows app, the LaunchBox plugin and the Pi program together; the plugin and the app share the named pipe, and Core and ROM need all three.
- Pi: `sudo apt install ./marquee-pi_*_all.deb` keeps the configuration, token and media. A manual installation also copies `static/fonts/` together with `marquee_pi.py` and the other files in `static/`.
- Windows: your `settings.ini` keeps working; the new `Theme` and `Navigation` keys are added the next time you save.

### Known limitations

- Beta: expect rough edges, especially outside the tested hardware.
- The buttons of the new Windows windows cannot be reached with the keyboard; Tab moves between the text fields, Enter saves and Esc cancels.
- The dashboard preview shows the game's marquee file or the default media file, not a live copy of the Pi display.
- The Core row needs RetroArch to be started with `-L` in the LaunchBox command line; other emulators show only the ROM.
- Everything from `1.0.0-beta.1` still applies: intermittent DSI detection at boot on the test Pi, WebP thumbnails depend on the Windows WebP extension, and the HTTP API is unencrypted (direct or trusted network only).

## 1.0.0-beta.1

Follows `1.0.0-preview.2`. It is tested on one setup (Raspberry Pi 3 B+ with the 7" DSI touch display on Raspberry Pi OS Trixie, Windows 11 arcade PC with LaunchBox/Big Box), not yet on others.

### Highlights

- **Touch menu on the Pi display.** Long press (or any swipe you choose) opens an overlay to switch the view, set the brightness, see the connection status and restart or shut down the Pi from a separate System page with confirmation.
- **Configurable gestures.** Long press is now a gesture like the four swipes. The new action *Open touch menu on Pi* can be assigned to any of them.
- **English and German.** The Windows app follows the Windows display language or a language you pick under Settings → General. The Pi touch menu uses the same language. All documentation is English.
- **Debian package for the Pi** (package `marquee-pi`, version `1.0.0~beta.1`). One `sudo apt install ./marquee-pi_*_all.deb` sets up the program, services, service user and a random API token; see `pi/INSTALL.md`.
- **Tabbed Windows settings** (Connection, Gestures, Media, RetroArch, General). The media manager is now the Media tab with thumbnails; for videos the thumbnail is a frame from the middle of the clip.

### Fixes

- Marquees and logos wider than 1600 px never reached the Pi because scaling failed silently. Artwork that cannot be prepared is now listed in `artwork-warnings.log`.
- Boot no longer stalls for 90 s waiting for `/dev/fb0`.
- Old artwork could show up after a Pi restart because the browser cached media by a counter that restarts at 1. Media are now cached by content.
- If rescaling large artwork fails, the original image is sent instead of nothing.
- Video playback no longer resets the connection when a file cannot be opened, and an `ffprobe` timeout is reported as an MP4 check error instead of a power error.
- Video thumbnails were sheared and upside down for clips with padded decoder buffers.

### Breaking changes

- Everything is now named Marquee-Pi: script `marquee_pi.py`, services `marquee-pi-api` and `marquee-pi-kiosk`, service user `marqueepi`, paths under `/opt/marquee-pi`, `/etc/marquee-pi` and `/var/lib/marquee-pi`, HTTP header `X-Marquee-Token`, named pipe `MarqueePiGameEvents`.
- The Windows app, the LaunchBox plugin and the Pi must all be from this release. Installations from the previous previews have to be reinstalled following `pi/INSTALL.md`.
- The Windows app no longer imports settings from the folder of the earlier name.

### Known limitations

- Beta: expect rough edges, especially outside the tested hardware.
- DSI detection on the test Pi 3 B+ was intermittent at boot; reseating the display flat cable fixed it there. See "Black DSI display on the Pi 3 B+" in `pi/INSTALL.md`.
- WebP thumbnails depend on the Windows WebP extension.
- The HTTP API is unencrypted; use a direct or trusted network only.
