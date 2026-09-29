# Installation on Raspberry Pi OS

This guide describes a fresh installation of Marquee-Pi on Raspberry Pi OS Lite (32-bit, currently Debian 13 "Trixie"). Tested on September 27, 2026 on a Raspberry Pi 3 B+ with an 800 × 480 DSI touch display and an 8 GB microSD card. For later OS versions, check package names and Polkit rules again.

## Boot medium and operating system

A fresh boot medium of at least 16 GB is recommended. On the tested 8 GB card, just under 3 GB remained free after the OS update, Chromium, X11, FFmpeg and `apt clean`. More space is advisable for additional videos and game artwork. The Pi 3 B+ can boot from microSD or USB mass storage. Use [Raspberry Pi Imager](https://www.raspberrypi.com/software/) to write **Raspberry Pi OS Lite (32-bit)** to the boot medium. Writing erases all existing data on it. In the Imager, set up user name, SSH access, Wi-Fi including country, time zone and hostname. A full desktop edition is not needed for the display. Keep the old boot medium until the function test has succeeded.

For a major version change, Raspberry Pi recommends a [fresh installation](https://www.raspberrypi.com/documentation/computers/os.html) rather than an upgrade of the running system. Do not blindly copy `config.txt` and `cmdline.txt` from an old Buster installation to Trixie: boot paths and graphics drivers have changed.

## Packages

After the first boot, log in via SSH and update:

```sh
sudo apt update
sudo apt full-upgrade -y
sudo apt install -y python3 ffmpeg fbi curl chromium xserver-xorg xserver-xorg-input-libinput xinit x11-xserver-utils xauth polkitd openssl
sudo apt clean
```

- `python3`: local API server without additional Python packages.
- `ffmpeg`: `ffprobe` checks uploaded MP4 files for H.264.
- `fbi`: shows the static boot splash in the Linux framebuffer before X11 is ready.
- `curl`: activates the stored shutdown media in the local kiosk on a direct Pi shutdown.
- `chromium`: full-screen display of the local web page.
- `xserver-xorg`, `xserver-xorg-input-libinput`, `xinit`, `xauth`: X11 session and touch input. The tested ft5x06 touchscreen was detected via libinput.
- `x11-xserver-utils`: `xset` disables the screen saver and DPMS.
- `polkitd`: tightly limited permission for Pi restart and shutdown.
- `openssl`: generate a random API token.

If Xorg does not start under the kiosk user, first check `journalctl -u marquee-pi-kiosk -b`. On some installations, `xserver-xorg-legacy` is additionally required; install and configure it only after a corresponding error.

## Network

The Pi needs no network to start the default media. For game artwork, Windows must be able to reach the Pi API on TCP port 8765. With a direct Ethernet connection, for example `10.0.0.1/24` can be used for Windows and `10.0.0.10/24` for the Pi; only **one** matching IP configuration may be active on this connection. On current Raspberry Pi OS, [NetworkManager](https://www.raspberrypi.com/documentation/configuration/) manages the connections. Determine the active Ethernet profile with `nmcli -g GENERAL.CONNECTION device show eth0` and then adjust it:

```sh
sudo nmcli connection modify '<ETHERNET-PROFIL>' ipv4.method manual ipv4.addresses 10.0.0.10/24 ipv4.never-default yes
sudo nmcli connection up '<ETHERNET-PROFIL>'
```

With a different network layout, change the addresses accordingly. Wi-Fi can be used in parallel for updates and SSH. The API access list in `config.json` should contain only the Windows IP.

The API deliberately uses plain HTTP; the header with the API token is not encrypted on the wire. Therefore, do not open port 8765 on the router or make it reachable from the internet. For the direct connection, use `"bind": "10.0.0.10"` and `"allowed_client_ips": ["10.0.0.1"]`. If the API should also be reachable via Wi-Fi, `bind` can stay at `0.0.0.0`, but `allowed_client_ips` must explicitly list all permitted Windows addresses. Firewall rules can additionally restrict access to the direct interface.

## Application and services

Run the following commands in the `pi` folder of a local copy of this repository on the Pi. A regular user must exist for the kiosk login; it is called `pi` here. If the name is different, set `PI_USER` accordingly.

```sh
PI_USER=pi
sudo useradd --system --user-group --no-create-home --shell /usr/sbin/nologin marqueepi
sudo install -d -o root -g root -m 755 /opt/marquee-pi
sudo cp -a marquee_pi.py start-kiosk.sh show-shutdown.sh static /opt/marquee-pi/
sudo chmod 755 /opt/marquee-pi/start-kiosk.sh /opt/marquee-pi/show-shutdown.sh
sudo install -o root -g root -m 755 configure-quiet-boot.sh /usr/local/sbin/marquee-pi-configure-quiet-boot
sudo install -d -m 755 /etc/X11/xorg.conf.d /etc/chromium/policies/managed
sudo install -m 644 xorg-modesetting.example.conf /etc/X11/xorg.conf.d/20-marquee-pi-modesetting.conf
sudo install -m 644 chromium-policy.example.json /etc/chromium/policies/managed/marquee-pi.json
sudo install -d -o marqueepi -g marqueepi -m 750 /var/lib/marquee-pi
sudo install -d -o root -g marqueepi -m 750 /etc/marquee-pi
sudo install -o root -g marqueepi -m 640 config.example.json /etc/marquee-pi/config.json
openssl rand -hex 32
```

Enter the printed token in `/etc/marquee-pi/config.json` (`sudo nano ...`). Set `allowed_client_ips` to the Windows IP, for example `["10.0.0.1"]`; leave `power_commands_enabled` at `false` for now. Token, personal media and local configuration do not belong in the Git repository.

```sh
sudo install -o root -g root -m 644 marquee-pi-api.service.example /etc/systemd/system/marquee-pi-api.service
sed "s/REPLACE_WITH_PI_USER/$PI_USER/g" marquee-pi-kiosk.service.example | sudo tee /etc/systemd/system/marquee-pi-kiosk.service >/dev/null
sudo chmod 644 /etc/systemd/system/marquee-pi-kiosk.service
sudo install -o root -g root -m 644 marquee-pi-boot-splash.service.example /etc/systemd/system/marquee-pi-boot-splash.service
sudo install -o root -g root -m 644 marquee-pi-shutdown-animation.service.example /etc/systemd/system/marquee-pi-shutdown-animation.service
sudo systemctl daemon-reload
sudo systemctl enable --now marquee-pi-api.service marquee-pi-kiosk.service marquee-pi-shutdown-animation.service
sudo systemctl enable marquee-pi-boot-splash.service
```

The kiosk template also starts on the Lite edition (`multi-user.target`). `start-kiosk.sh` sets the X11 screen saver to timeout 0, disables DPMS and uses software rendering to relieve the GPU of the Pi 3 B+. The Xorg configuration selects only the `modesetting` driver for DSI and prevents a second `fbdev` screen. The Chromium policy disables the translation bar. Verify with:

```sh
systemctl is-active marquee-pi-api marquee-pi-kiosk
sudo -u "$PI_USER" env DISPLAY=:0 XAUTHORITY="/home/$PI_USER/.Xauthority" xset q
sudo -u "$PI_USER" env DISPLAY=:0 XAUTHORITY="/home/$PI_USER/.Xauthority" xrandr --current
```

## Boot splash and quiet system startup

The Windows tool later uploads the boot splash via **Manage media… > Use as boot splash**. It must be a PNG or JPEG and is stored as `/var/lib/marquee-pi/boot-splash`. Until a custom image has been chosen, a PNG can be copied manually to this location, for example:

```sh
sudo install -o marqueepi -g marqueepi -m 640 boot.png /var/lib/marquee-pi/boot-splash
```

Then hide the normal console text. The script saves the original kernel command line once as `/boot/firmware/cmdline.txt.marquee-pi-before-quiet-boot`, checks the required `root=` entry before the atomic replacement, adds the quiet startup options and disables the normal getty on tty1. `console=tty1` is deliberately retained so that kernel, boot and file system errors remain visible; after a normal start, no login prompt appears there because the getty is disabled:

```sh
sudo marquee-pi-configure-quiet-boot
sudo reboot
```

The serial console and tty1 are retained for early diagnostic messages. Very early firmware output before the Linux framebuffer, as well as actual boot errors, can therefore be visible. As soon as `/dev/fb0` is available, `marquee-pi-boot-splash.service` shows the static image; X11 then takes over with the default media.

Undo quiet boot:

```sh
sudo cp -p /boot/firmware/cmdline.txt.marquee-pi-before-quiet-boot /boot/firmware/cmdline.txt
sudo systemctl enable getty@tty1.service
sudo reboot
```

If the Pi no longer starts, open the FAT boot partition with a card reader on Windows and copy `cmdline.txt.marquee-pi-before-quiet-boot` back as `cmdline.txt`. The file must remain a single line.

## Setting up media via the Windows tool

In the Windows tray, open **Manage media…**, add a file and choose a role:

- **Use as default:** JPG, PNG, GIF, WebP or H.264 MP4; appears when no game is running.
- **Use as boot splash:** PNG or JPEG; appears before the kiosk from the next Pi start.
- **Use as shutdown media:** JPG, PNG, GIF, WebP or H.264 MP4; appears before power-off.

A shutdown video plays once. The Pi determines the duration at upload time and stores it in the media manifest. On shutdown, it waits this duration plus one second, at least four and at most 30 seconds. If the Pi is shut down directly with `systemctl poweroff`, `marquee-pi-shutdown-animation.service` activates the local display before the API and kiosk are stopped. On a restart, the shutdown animation is skipped. On a shutdown from the Windows tool or during the Windows shutdown, the API switches to the media already before the actual poweroff.

## Testing and resetting the display

The helper script `marquee-display-test.sh` can be installed as `/usr/local/sbin/marquee-display-test`. It requires root privileges for `blink` and `reset`:

```sh
sudo install -o root -g root -m 755 marquee-display-test.sh /usr/local/sbin/marquee-display-test
marquee-display-test status
sudo marquee-display-test blink
sudo marquee-display-test reset
```

`status` shows DSI, touch, X11 and service status. `blink` switches a detected display off for three seconds and back on. `reset` resets an existing DSI output and the kiosk. If DSI is missing entirely, `reset` performs a single warm restart of the Pi; the display power supply must stay on during this.

## Black DSI display on the Pi 3 B+

If `card0-DSI-1` is missing after a cold start and only `Composite-1` appears instead, the display board may not have been ready yet during the early firmware detection. On the tested Pi, the 7-inch display was powered via a separately supplied USB hub. An unchanged FKMS warm restart detected the display reliably.

As a permanent solution, back up the existing `/boot/firmware/config.txt` and enter `bootcode_delay=10` directly in this file:

```ini
bootcode_delay=10
display_auto_detect=1
dtoverlay=vc4-fkms-v3d
#disable_fw_kms_setup=1
```

`bootcode_delay` gives the display board ten seconds of additional startup time before detection. Five seconds initially passed three cold starts, but the non-detection later occurred again. The target system has therefore since used ten seconds.

If FKMS uses the 720×480 composite output as the primary framebuffer despite DSI being detected, first back up the current kernel command line. Then add the following in the same single line of `/boot/firmware/cmdline.txt`:

```text
video=Composite-1:d video=DSI-1:800x480@60
```

Before restarting, check that the line still contains the existing `root=` parameter. On the target system, this setting resulted in `DSI-1 connected primary 800x480` and `Composite-1 disconnected`. The quiet boot script retains both `video=` parameters.

Check the status with:

```sh
ls /sys/class/drm/
ls /dev/input/
marquee-display-test status
sudo -u "$PI_USER" env DISPLAY=:0 XAUTHORITY="/home/$PI_USER/.Xauthority" xrandr --current
```

In case of failure, `sudo marquee-display-test reset` can be used as a fallback. If DSI is missing entirely, the script restarts the Pi warm once while the display board stays powered on the powered hub.

Switching to full KMS was not a solution on the test device. The fixed configuration with `vc4-kms-v3d`, `vc4-kms-dsi-7inch`, `ignore_lcd=1` and `disable_touchscreen=1` did detect DSI and the backlight interface, but the real display stayed black. Kernel messages showed I/O errors when enabling the backlight and on the touch controller. FKMS with `bootcode_delay=10` and the fixed DSI selection therefore remains the tested configuration for this device. Other Pi models and display variants may require KMS.

## Default video on a Pi 3 B+

For MP4 videos, use H.264 with `yuv420p` and adapt the image size to the 800 × 480 display where possible. An uploaded 1254 × 1254 H.264 video could not be played reliably in the Chromium kiosk on the test device; a 480 × 480 version worked. Example for a square template:

```sh
ffmpeg -i input.mp4 -vf "scale=480:480:flags=lanczos" -c:v libx264 -preset veryfast -profile:v baseline -level 3.0 -pix_fmt yuv420p -crf 23 -an output.mp4
```

Upload the file `output.mp4` as default media via the Windows tool. Keep the original outside the Pi data folder.

## Restart and shutdown via the Windows tool

Trixie uses JavaScript rules under `/etc/polkit-1/rules.d`; the old Buster file `marquee-pi.pkla.example` does not apply here. The template permits only the service user `marqueepi` the four required login1 actions.

```sh
sudo install -o root -g root -m 644 marquee-pi.rules.example /etc/polkit-1/rules.d/50-marquee-pi.rules
PID=$(systemctl show -p MainPID --value marquee-pi-api)
START_TIME=$(python3 -c 'import sys; print(open(sys.argv[1]).read().rsplit(")", 1)[1].split()[19])' "/proc/$PID/stat")
PROCESS="$PID,$START_TIME,$(id -u marqueepi)"
sudo -u marqueepi pkcheck --action-id org.freedesktop.login1.reboot --process "$PROCESS"
sudo -u marqueepi pkcheck --action-id org.freedesktop.login1.power-off --process "$PROCESS"
```

The check must be performed for the service user; both `pkcheck` calls must succeed. Only then set `power_commands_enabled` in `config.json` to `true` and restart the API service. Test the API restart first. The shutdown test comes last because the Pi only starts again afterwards through a new power cycle. `NoNewPrivileges=true` in the service file remains active.

## Function test

1. Upload a static boot image in the Windows tool. Reboot without a Windows connection: the boot splash and then the stored default image or video appear automatically; no normal console text is shown in between.
2. Check that `xset q` reports `timeout: 0` and `DPMS is Disabled`, `xrandr` shows `DSI-1 connected 800x480` and the display stays visible for at least ten minutes.
3. Connect the Windows tool with the Pi IP and token. Start a game in LaunchBox/Big Box: the matching marquee appears.
4. Tap the display: if controls artwork is available, switch between marquee and control panel. The four swipe gestures are configured in the Windows tool; the chosen image view appears immediately.
5. Exit the game: the default media appears. Test a Pi restart via the tray menu; the media appears again after booting.
6. Upload a shutdown media file. Perform the Pi shutdown via the tray menu only after all other tests: the media appears in full or for at most 30 seconds, after which the Pi is no longer reachable via ping/SSH.

In case of errors, read `journalctl -u marquee-pi-api -u marquee-pi-kiosk -b --no-pager`. For a community installation, adapt your own IP addresses, display orientation, user name and default media.
