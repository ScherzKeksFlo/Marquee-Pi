# Marquee-Pi for Windows

The Windows tray tool is a native C++20 application for Windows 11. It receives game events from the LaunchBox plugin via a named pipe, sends media to the Pi API and shows the connection, settings and default media in the notification area. The EXE does not require a .NET runtime. The LaunchBox plugin remains a .NET Framework DLL because it uses the LaunchBox plugin API.

## Build

You need MinGW-w64 with `g++` and `windres` (tested with GCC 13.1), PowerShell and, for the plugin DLL, the .NET SDK and a local LaunchBox installation. The proprietary LaunchBox API DLL is only referenced during the build and is not included.

```powershell
./windows/native/build.ps1 -LaunchBoxRoot "C:\LaunchBox"
```

The script builds the native EXE, runs JSON/INI/hotkey tests and creates `dist/Marquee-Pi/Marquee-Pi-portable-win-x64.zip`. The ZIP contains the EXE, LaunchBox plugin DLL, `portable.flag`, instructions and license. It contains no tokens or personal media. The EXE statically links the C++ and GCC runtimes and imports only Windows system DLLs.

## Look and feel

The windows are drawn by the program itself (GDI+, no extra runtime) in an arcade theme. The fonts Silkscreen, Chakra Petch and IBM Plex Mono are bundled in the EXE under the SIL Open Font License; the license texts are in `windows/Resources/fonts/`. Two optional `[General]` settings in `settings.ini` change the look:

- `Theme`: `dark` (default), `light` or `auto` (follows the Windows app theme).
- `Navigation`: `sidebar` (default) or `tabs`.

The window scales with the display (per-monitor DPI). The Logs page keeps the last 500 events in memory; `shutdown.log` and `artwork-warnings.log` in the data folder are unchanged.

## Installation and configuration

For the portable version, unpack the ZIP and start `Marquee-Pi.exe`. The `portable.flag` file next to the EXE causes `Data/settings.ini` and `Data/media/` to be located in the same folder. Without this file, the data is stored under `%LOCALAPPDATA%\Marquee-Pi\`.

The plugin DLL `MarqueePiLaunchBox.dll` belongs in `LaunchBox\Plugins\Marquee-Pi\`. Then restart LaunchBox/Big Box. The plugin and the Windows app talk over the named pipe `MarqueePiGameEvents`, so update both together. For emulated games the plugin also reports the ROM file name and, for RetroArch, the libretro core (read from the `-L` argument of the launch command line; the readable name comes from RetroArch's `info\*.info` file when it exists). The Pi shows both in the touch menu.

Clicking the tray icon opens a flyout with the connection status, the default media and the commands below; **Settings…** opens a window with seven pages: **Dashboard** (what the Pi shows, connection, current game, active media roles, quick actions, recent events), **Connection** (Pi address, API token, *Test connection*), **Media** (media management), **Gestures** (long press and the four swipe directions), **RetroArch** (control type, key combination, network port), **General** (language, autostart, INI file) and **Logs** (recent events with level filter, copy to clipboard). Each gesture can be assigned an action: a view, the RetroArch menu or **Open touch menu on Pi**. By default, the long press opens the touch menu; it can be moved to a swipe gesture instead, so that the long press becomes free for another action. When saving, the program warns if no gesture opens the touch menu. The INI setting is called `LongPress` (default `touch_menu`). The interface language is chosen under **General → Language**: **Automatic** follows the Windows display language, or pick **English** or **Deutsch**. The choice applies immediately to the tray menu and dialogs and is also sent to the Pi, so the touch menu on the display uses the same language. The INI setting is `Language` in `[General]` (`auto`, `en` or `de`; default `auto`, and English when Windows is neither). **Help: create token** shows the steps on the Pi. **Open INI file** and **Reload settings** allow direct file editing. The token is stored in plain text in the INI and does not belong in the Git repository.

For the **Open RetroArch menu** action, Marquee-Pi can either send the configured keyboard shortcut or use RetroArch's local network command `MENU_TOGGLE`. For network mode, enable network commands in RetroArch under **Settings > Network > Network Commands** or set `network_cmd_enable = "true"` in `retroarch.cfg`, and restart RetroArch. The port must match `network_cmd_port` (default: 55355). Marquee-Pi only contacts `127.0.0.1`; depending on the firewall, RetroArch's network feature may also be reachable from the LAN.

**Windows Firewall on the first RetroArch start:** After enabling network commands, Windows may ask for permission for `retroarch.exe`. Marquee-Pi sends the command only to `127.0.0.1`. Test first without additional permission. If the local menu action then does not work, allow RetroArch at most on trusted private networks; leave "Public networks" unchecked. Do not deliberately open the network command port to other devices. Background: [Windows Firewall profiles](https://learn.microsoft.com/windows/security/operating-system-security/network-security/windows-firewall/).

The **Media** page (also reachable via **Manage media…** in the tray flyout and by double-clicking the tray icon) imports JPG, PNG, GIF, WebP and MP4 up to 20 MB. Each file appears with a thumbnail, type, size and its current roles; for videos, the thumbnail is a frame from the middle of the clip. The thumbnails are created in the background. If a file cannot be decoded (for WebP this depends on the Windows WebP extension), "no preview" is shown. A file can be assigned to one or more roles:

- **Use as default:** Logo or animation that the kiosk shows when no game is running.
- **Use as boot splash:** Static PNG or JPEG for the time between kernel start and kiosk. The change takes effect from the next Pi start.
- **Use as shutdown media:** Image, GIF, WebP or H.264 MP4 that appears before power-off. An MP4 plays once; the shutdown waits for its duration, at most 30 seconds.

A file in use can only be deleted after a replacement has been chosen for each of its roles. All three roles are stored on the Pi and need no Windows connection when displayed. The tray also offers Show default logo, Reload display, Restart Pi and Shut down Pi. On a full Windows shutdown, the Pi is shut down only after a matching Windows event; a Windows restart leaves it on. Diagnostic messages are in `shutdown.log` in the respective data folder. `Marquee-Pi.exe --pi-shutdown` sends the command manually.

## Verification

The native EXE was built with GCC 13.1. JSON/INI/hotkey tests, a window smoke test and a real game start/game end message via the named pipe to the Pi API were successful. The local RetroArch network command was tested successfully on the arcade PC with a swipe gesture during a running game. The full shutdown via Big Box was tested on the arcade PC: the Pi shut down and its API was no longer reachable afterwards. The Pi shutdown command was also sent when shutting down via the regular Windows Start menu; the Pi was subsequently no longer reachable by ping.
