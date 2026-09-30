# Marquee-Pi

Vocabulary for the Windows tool, the LaunchBox plugin and the Pi display.

**Game session**: the game LaunchBox reported as running, with its artwork. It exists on Windows from game start to game exit, and the Pi shows it until a heartbeat is missed for 60 seconds.

**Default media**: the logo, video or animation the Pi shows when no game session is active. Stored on the Pi, so it needs no Windows connection.

**Pi sync**: reconciling what Windows wants the Pi to show (game session, gesture and language settings) with what the Pi has. Implemented by `PiSync` in `windows/native/pi_sync.cpp`; the seam to the Pi is `PiTransport`.
