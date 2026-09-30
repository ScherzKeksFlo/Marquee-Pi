# Marquee-Pi

Vocabulary for the Windows tool, the LaunchBox plugin and the Pi display.

**Game session**: the game LaunchBox reported as running, with its artwork. It exists on Windows from game start to game exit, and the Pi shows it until a heartbeat is missed for 60 seconds.

**Default media**: the logo, video or animation the Pi shows when no game session is active. Stored on the Pi, so it needs no Windows connection.

**Display detection**: the search that finds the screens connected to the Pi and proposes them for selection. It only proposes; nothing changes until the user confirms.

**Display profile**: the confirmed choice of exactly one screen for Marquee-Pi, with how it is used: mode, rotation, how the picture fits the screen, scale and the touch device that belongs to it. Other connected screens stay switched off.

**Touch display**: a screen whose display profile has a touch device. The touch menu and the gestures work only here.

**View-only display**: a screen without a touch device. It shows the marquee and nothing else; all control comes from Windows.

**Pi sync**: reconciling what Windows wants the Pi to show (game session, gesture and language settings) with what the Pi has. Implemented by `PiSync` in `windows/native/pi_sync.cpp`; the seam to the Pi is `PiTransport`.
