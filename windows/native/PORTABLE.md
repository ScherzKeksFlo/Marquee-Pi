# Marquee-Pi – portable Windows version

1. Unpack the ZIP completely into a folder. Start `Marquee-Pi.exe`.
2. In the notification area, open **Settings…** and enter the Pi address and the API token. The **Help: create token** button explains the setup on the Pi.
3. Copy the LaunchBox plugin `MarqueePiLaunchBox.dll` to `LaunchBox/Plugins/Marquee-Pi/`. Then restart LaunchBox/Big Box.

Under **Manage media…**, each imported file can be transferred to the Pi as default media or shutdown media. A static PNG or JPEG is required for the early boot splash. The shutdown media may also be a GIF, WebP or H.264 MP4. A video is played once on shutdown; after 30 seconds at the latest, the Pi continues.

The `portable.flag` file enables portable mode. Marquee-Pi creates `Data/settings.ini` and `Data/media/` in the same folder. When the entire folder is copied, settings and media are carried along. The INI contains the token in plain text and should not be published.

On Windows 11, the EXE needs no separately installed .NET runtime and no installer. The optional autostart stores the absolute EXE path in the user account; after moving the folder, save the autostart setting again in the settings window.

For the Raspberry Pi, the installation guide in the Git repository applies. The Pi address and token must be reachable and identical to the Pi configuration.

For the swipe action "Open RetroArch menu", the settings window lets you choose between keyboard shortcut and local network command. For the network command, set `network_cmd_enable = "true"` in RetroArch, match the port (default 55355) and restart RetroArch.

On the first start after enabling RetroArch network commands, Windows Firewall may ask for access for `retroarch.exe`. Marquee-Pi uses only `127.0.0.1`: test without permission first; if necessary, allow only trusted private networks and do not select "Public networks".
