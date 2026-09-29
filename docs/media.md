# Media management

## Role of the Windows tool

The Windows tool manages a shared library of custom media. It can add files, show a preview and transfer a file to the Pi as default media, boot splash or shutdown media. A file may be assigned to several roles. As long as it is active in at least one role, it can only be deleted locally after a replacement has been chosen. After an upload, the Pi visibly reports format errors and storage problems back to Windows.

The Pi keeps the most recently activated default media locally. This makes it appear as soon as the Pi starts, before Windows is reachable. A new media file is first transferred completely and verified; only then does it replace the active media. If an upload fails, the existing default media is kept.

## Startup without Windows

The selected file and the information about which file is active are stored permanently on the Pi. The display program starts automatically at boot and loads this media from local storage; it waits neither for the network nor for LaunchBox. An MP4 or animation starts in an endless loop as soon as the graphical output is ready. If the active file is corrupted or unreadable, a bundled local fallback image appears.

The static boot image is also stored permanently on the Pi. The framebuffer shows it until the X11 kiosk takes over the output. The kernel startup is largely hidden with the documented `cmdline.txt` options. Video and animation only start with Chromium; the early splash therefore accepts only PNG or JPEG.

The shutdown media is shown in the already running kiosk before `systemctl poweroff`. An H.264 MP4 plays once. The detected video duration plus a short margin determines the wait time, capped at 30 seconds. On a direct shutdown at the Pi, a dedicated systemd service ensures the same sequence.

## Formats of the first version

| Extension | Use | Notes |
| --- | --- | --- |
| `.jpg`, `.jpeg` | Photo, static logo, boot splash | No transparency |
| `.png` | Static logo, boot splash | Transparency possible |
| `.gif` | Short animation | Limit resolution and frame rate for the Pi 3 B+ |
| `.webp` | Static logo | Enable animated WebP only after testing on the Pi |
| `.mp4` | Video in an endless loop | H.264 video stream; audio is ignored |

A file extension alone is not enough: the tool checks the actual media type, and for MP4 also the video codec, and reports unsupported files before activation. The prototype has a fixed limit of 20 MB per file. Limits for resolution and animation duration will be defined after the device test. The target screen has 800 × 480 pixels; media is scaled proportionally to fit, without cropping anything by default. Videos are stopped when switching to a game and restarted when returning to the default media.

APNG can be added later if animated transparency is needed. SVG can be converted to PNG on import if required. For the first version, additional video containers and H.265/VP9 are not planned, as they offer no advantage for this screen on the Pi 3 B+.

## Persistence and SD card protection

An optional read-only overlay file system discards ordinary changes on restart. For default media uploaded by the Windows tool, the Pi therefore needs an explicitly persistent storage location, for example a separate writable data partition. The installation process must set up and verify this location before uploads are enabled. Media changes are atomic: write the new file, verify it, then change the reference to the active media.

## Open device tests

- Preview and endless loop for GIF and H.264 MP4 on the actual Pi 3 B+
- Behavior on touch input during an animation
- Upload and Pi restart with the read-only overlay active
- Fallback to the previous default media after an aborted upload
