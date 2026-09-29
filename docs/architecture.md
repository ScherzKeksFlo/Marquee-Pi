# Architecture and acceptance criteria

## Responsibilities

1. The Pi program starts automatically at boot, loads the default media permanently stored on the Pi without a network connection, and shows a local fallback image if a file error occurs.
2. A Windows program provides the connection, status and the menu in the notification area. It sends image data and commands to the Pi.
3. A LaunchBox/Big Box plugin reports game start and game end to the Windows program. The plugin interface provides image types such as `Arcade - Marquee` and `Arcade - Controls Information`.
4. The Windows shutdown control distinguishes a full shutdown from a restart. This distinction must not be derived from the loss of the network connection alone.

## Display states

| State | Pi display | Short tap |
| --- | --- | --- |
| Startup / no game | Local default logo or animation | No change |
| Game active, marquee available | Marquee | Switch to the control panel view, if available |
| Game active, no marquee | Banner or logo; otherwise default image | Switch to the control panel view, if available |
| Control panel view | Controls artwork belonging to the game | Return to the game artwork |
| Game end | Default media | No change |
| Windows connection lost | Default media after a timeout expires | No change |

The artwork is scaled proportionally to fit 800 × 480. Cropping is disabled by default. New game events discard older display states. After the connection is re-established, Windows synchronizes the current state.

## Touch gestures

The Pi program recognizes a short tap and four separate swipe gestures: top to bottom, bottom to top, left to right and right to left. For each recognized gesture, an event with the direction and the current display state is generated. The swipe gestures are assigned individually in the Windows tool. The Pi handles image actions locally; it reports RetroArch gestures to the Windows app. An unassigned gesture does not change the display.

Detection uses the start, movement and end of a single touch. The minimum distance, maximum duration and permitted lateral movement should be configurable. A swipe must not trigger a tap; a short tap must not trigger a swipe. Multiple simultaneous touches are initially ignored. Details and acceptance criteria are in [touch.md](touch.md).

## Default media

The Windows tool manages its own default images and animations, shows a preview and transfers the selected media file to the Pi. The Pi stores the active media file permanently and locally so that it is visible even before Windows starts. A failed upload must not replace the existing media file. Formats, limits and the persistent storage location when the overlay is enabled are described in [media.md](media.md).

## Pi control commands

- `reload`: Reload the active default media from local storage without an operating system restart.
- `show-default`: Show the default media.
- `reboot`: Restart the Pi cleanly.
- `shutdown`: Shut the Pi down cleanly. Starting it again requires a new power cycle of the wireless power socket.
- `status`: Query reachability, program version, current state and active game.

The concrete network API is described in [protocol.md](protocol.md). On the tested Pi, Polkit only permits reboot and power-off for the API service's own user. It remains restricted to the direct connection and requires authentication. Local credentials are not stored in Git.

## Shutdown

The Windows Start menu and the Big Box menu should trigger the same sequence: shut down the Pi, then shut down Windows, and finally switch off the wireless power socket manually. No Pi shutdown may be triggered on a Windows restart. A general Windows shutdown script is not suitable because it also runs on restarts. The tray app handles `WM_ENDSESSION` only for a confirmed session end without logoff or app restart. It then reads the most recent User32 event 1074 from the system log that was written since the app started and at most two minutes ago. Known restart terms in several Windows languages are excluded first. Only the clearly supported power-off types `shutdown`, `power off`, `herunterfahren` and `ausschalten` send a Pi shutdown with a three-second time limit. With a different Windows display language, a restart, a missing event or a read error, the Pi stays on as a precaution; the detected type is then recorded in `shutdown.log`.

## Compatibility names

The visible application and new files are named Marquee-Pi. The named pipe `ArcadePiDisplayGameEvents`, the HTTP header `X-Arcade-Token`, the Pi script `arcade_pi.py` and the installation paths `/opt/arcade-pi-display` and `/var/lib/arcade-pi-display` intentionally remain during protocol version 1. This keeps older plugin versions and existing Pi installations working. The Windows tool also imports settings once from `%LOCALAPPDATA%\ArcadePiDisplay`. These identifiers will be removed no earlier than with a new, incompatible major protocol version.

A Pi overlay file system can additionally protect the SD card against accidental early power-off. It does not replace the orderly shutdown sequence. Local configuration and media must be prepared before the read-only overlay is enabled.

## To be checked on site

- Pi OS version and 32-/64-bit architecture
- Boot test with Windows switched off: the stored default media appears without a network; video or animation starts once the graphical output is available
- Actual display orientation and touch coordinates
- Static IP addresses or fixed names of the direct connection
- LaunchBox installation path, version and available image types per sample game
- Behavior of the Windows Start menu and the Big Box menu on shutdown and restart
- Time from the Pi shutdown command to safe standstill
- Whether the wireless power socket is switched off after Windows has fully stopped

## Community package

Release artifacts should contain the Windows app, plugin, Pi installation package, sample files and checksums. Neither ROMs nor game artwork, credentials or LaunchBox binaries belong in the repository or release. Installation, update, uninstallation and recovery after a lost connection must be documented and tested.
