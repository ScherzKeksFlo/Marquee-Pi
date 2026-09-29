# Debian package

`build_deb.py` builds `marquee-pi_<version>_all.deb` for Raspberry Pi OS (Debian 13 "Trixie") from the files in `pi/`. It needs only Python 3 and runs on Windows, Linux and macOS:

```sh
python3 packaging/deb/build_deb.py            # writes dist/deb/marquee-pi_<version>_all.deb
python3 packaging/deb/build_deb.py --output /tmp/out
```

The version comes from `APP_VERSION` in `pi/marquee_pi.py`; a pre-release such as `1.0.0-beta.1` becomes `1.0.0~beta.1`, which Debian sorts before `1.0.0`. The build is reproducible: file times come from `SOURCE_DATE_EPOCH` or the last commit, and everything is owned by root.

## What the package does

- Installs the program to `/opt/marquee-pi`, the four systemd services to `/usr/lib/systemd/system`, the Polkit rule to `/usr/share/polkit-1/rules.d`, the Chromium policy and the Xorg configuration to `/etc` (both are conffiles), and the helpers `marquee-pi-configure-quiet-boot` and `marquee-display-test` to `/usr/sbin`.
- `postinst` creates the system user `marqueepi`, the directories `/var/lib/marquee-pi` and `/etc/marquee-pi`, generates `/etc/marquee-pi/config.json` with a random token on the first install (never on upgrades), picks the kiosk login user, enables and starts the services.
- Dependencies (`Depends`) cover every program the scripts call, so `sudo apt install ./marquee-pi_….deb` pulls in whatever is missing. Plain `dpkg -i` does not resolve dependencies; if it was used, `sudo apt -f install` completes the installation. `libraspberrypi-bin` (for `vcgencmd`) is only recommended because it exists only in the Raspberry Pi OS repository.
- `prerm` stops and disables the services on removal. `postrm` reloads systemd; on `purge` it also deletes the configuration, the stored media and the service user.

The boot configuration (`config.txt`, `cmdline.txt`, quiet boot) is deliberately not touched by the package. See `pi/INSTALL.md`.

## Checks

`pi/tests/test_deb.py` builds the package and inspects it: archive layout, control fields, owners and modes, md5sums, conffiles, line endings, unit paths, syntax of the maintainer scripts and reproducibility. CI additionally runs `dpkg-deb`, `lintian` (informational) and installs and purges the package on an Ubuntu runner to execute the maintainer scripts.
