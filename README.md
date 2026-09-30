# Marquee-Pi

A companion display for LaunchBox/Big Box on Windows 11 and a Raspberry Pi with a display. Tested with a Raspberry Pi 3 B+ and the 7-inch touch display (800 × 480); HDMI and DSI displays of other sizes, with or without touch, are supported through the display profile (see [docs/displays.md](docs/displays.md)).

> Project status: The Pi display and the Windows tray program are installed on the target system. A real game launch from Big Box showed the matching marquee and switched back to the default animation when the game ended. Automatic Pi shutdown was tested successfully when shutting down via Big Box and via the Windows Start menu.

## Features

- After power-on, the Pi shows a locally stored static boot image. As soon as the kiosk is ready, it loads the permanently stored default logo, video or animation without a Windows connection.
- When a game starts, the Windows program sends the game identifier and matching artwork to the Pi. The display shows a marquee, or alternatively a banner or logo.
- A short tap switches between the marquee and the control panel view during the game. Four swipe directions (top to bottom, bottom to top, left to right, right to left) can be assigned independently in the Windows tool to image views or the RetroArch menu. For RetroArch, a freely configurable keyboard shortcut and a local network command are available. When the game ends, the default logo appears again.
- When Windows restarts, the Pi stays on; a lost connection only resets the display.
- A Windows icon in the notification area shows the connection status. Its flyout offers Reload display, Restart Pi, Shut down Pi and Show default logo, and opens the settings window with a dashboard, the media manager (default media, boot splash and shutdown media independently, with upload progress) and a log of recent events.
- On a full shutdown via Big Box or the Windows Start menu, the Pi shows the stored shutdown image or video and then shuts itself down. The wireless power socket can then be switched off manually.

## Components

| Folder | Purpose |
| --- | --- |
| `windows/` | Native C++ tray tool with notification area icon, communication and shutdown control |
| `launchbox-plugin/` | LaunchBox/Big Box plugin for game start and game end |
| `pi/` | Full-screen display, touch control and local receiver |
| `docs/` | Architecture, protocol, installation and tests |

The portable Windows package contains the EXE, plugin DLL, instructions and license. IP addresses, installation paths, credentials and custom media are not hard-coded. Game artwork and LaunchBox binaries are not included.

The Windows-to-Pi connection uses HTTP. The API token is also transmitted unencrypted. Marquee-Pi is therefore intended for a direct point-to-point connection or a trusted local network and must not be forwarded to the internet. On the Pi, `allowed_client_ips` should be restricted to the IP of the arcade PC, or `bind` should be set to the address of the direct network interface.

## Quick start

1. [Install Raspberry Pi OS Lite and set up the Pi services](pi/INSTALL.md). Generate your own API token and store it in the Pi configuration.
2. Unpack the portable Windows package from the GitHub releases. [Set up the Windows tool](windows/README.md) and enter the Pi address and the same token.
3. Copy the included LaunchBox plugin DLL to `LaunchBox\Plugins\Marquee-Pi\` and [restart LaunchBox/Big Box](launchbox-plugin/README.md).
4. Under **Manage media…**, define a default media file, a static PNG/JPEG as the boot splash and optionally a shutdown media file.
5. Start a game and check the display. For the RetroArch menu swipe gesture, note the [local network mode and the possible firewall dialog](windows/README.md).

Automatic Pi shutdown was tested in practice both when shutting down via Big Box and via the regular Windows Start menu. Pi restart and Pi shutdown from the tray menu are separate functions.

On the first target system, the display board, which is powered through its own USB hub, needed additional time after a cold start. After sporadic non-detection occurred again later, `bootcode_delay=10` was used together with a fixed DSI selection; the test script additionally offers a warm restart as a fallback. Details are in the [Pi installation guide](pi/INSTALL.md).

## Hardware of the first target system

- Windows 11 with LaunchBox/Big Box and RetroArch
- Raspberry Pi 3 B+
- Raspberry Pi 7-inch Touch Screen Display, 800 × 480
- Direct network connection between Windows and the Pi
- Shared wireless power socket, switched off manually after shutdown

Other Pi models, displays and network addresses require a suitable local configuration (on the Pi, `marquee-pi-configure-display` helps with the display); only the hardware listed above has been tested so far.

## Development

The interface and the open hardware checks are described in [docs/architecture.md](docs/architecture.md). Formats and upload rules are described in [docs/media.md](docs/media.md). Start and build steps are in the README files of the components; the Pi reinstallation is described in [pi/INSTALL.md](pi/INSTALL.md). The source code is licensed under the [MIT license](LICENSE).
