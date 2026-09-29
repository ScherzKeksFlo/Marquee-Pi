# Changelog

## 1.0.0-beta.1

Follows `1.0.0-preview.2`. It is tested on one setup (Raspberry Pi 3 B+ with the 7" DSI touch display on Raspberry Pi OS Trixie, Windows 11 arcade PC with LaunchBox/Big Box), not yet on others.

### Highlights

- **Touch menu on the Pi display.** Long press (or any swipe you choose) opens an overlay to switch the view, set the brightness, see the connection status and restart or shut down the Pi from a separate System page with confirmation.
- **Configurable gestures.** Long press is now a gesture like the four swipes. The new action *Open touch menu on Pi* can be assigned to any of them.
- **English and German.** The Windows app follows the Windows display language or a language you pick under Settings → General. The Pi touch menu uses the same language. All documentation is English.
- **Debian package for the Pi** (`marquee-pi_1.0.0~beta.1_all.deb`). One `sudo apt install ./marquee-pi_….deb` sets up the program, services, service user and a random API token; see `pi/INSTALL.md`.
- **Tabbed Windows settings** (Connection, Gestures, Media, RetroArch, General). The media manager is now the Media tab with thumbnails; for videos the thumbnail is a frame from the middle of the clip.

### Fixes

- Marquees and logos wider than 1600 px never reached the Pi because scaling failed silently. Artwork that cannot be prepared is now listed in `artwork-warnings.log`.
- Boot no longer stalls for 90 s waiting for `/dev/fb0`.
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
