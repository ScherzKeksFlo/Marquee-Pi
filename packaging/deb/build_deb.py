#!/usr/bin/env python3
"""Build the marquee-pi Debian package without dpkg tools.

A .deb is an `ar` archive with `debian-binary`, `control.tar.gz` and `data.tar.gz`,
so this script runs anywhere Python 3 does (Windows, macOS, Linux, CI) and produces
the same bytes for the same sources: file times come from SOURCE_DATE_EPOCH (or the
last commit) and all files are owned by root.

    python3 packaging/deb/build_deb.py [--output DIR] [--version VERSION]
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import os
import re
import subprocess
import sys
import tarfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PI = ROOT / "pi"
DEBIAN = HERE / "debian"

PACKAGE = "marquee-pi"
MAINTAINER = "Keks <flo@scherz-keks.de>"
HOMEPAGE = "https://github.com/ScherzKeksFlo/Marquee-Pi"
DEPENDS = [
    "python3", "ffmpeg", "fbi", "curl", "chromium | chromium-browser", "xserver-xorg",
    "xserver-xorg-input-libinput", "xinit", "x11-xserver-utils", "xauth",
    "polkitd | policykit-1", "openssl", "adduser", "hostname", "systemd",
]
# vcgencmd (marquee-display-test) comes from a Raspberry Pi OS package that Debian does not have.
RECOMMENDS = ["libraspberrypi-bin"]
DESCRIPTION = (
    "Marquee display for arcade cabinets on a Raspberry Pi",
    "Shows game marquees, box art and controls on a small touch display attached",
    "to a Raspberry Pi. The Windows app sends the artwork of the running game",
    "over HTTP; a local kiosk browser displays it and offers a touch menu for",
    "views, brightness, status and restart or shutdown.",
    "",
    "This package installs the API server, the kiosk session, the systemd",
    "services and the helper scripts. The Windows app and the LaunchBox plugin",
    "come from the same release.",
)

TEXT_SUFFIXES = {".py", ".sh", ".js", ".html", ".svg", ".json", ".md", ".conf", ".service",
                 ".rules", ".example", ".txt", ""}


def deb_version(app_version: str) -> str:
    """1.0.0-beta.1 -> 1.0.0~beta.1 so that it sorts before 1.0.0."""
    return re.sub(r"-(?=[A-Za-z])", "~", app_version)


def app_version() -> str:
    match = re.search(r'^APP_VERSION = "([^"]+)"', (PI / "marquee_pi.py").read_text(encoding="utf-8"), re.M)
    if not match:
        raise SystemExit("APP_VERSION not found in pi/marquee_pi.py")
    return match.group(1)


def source_date_epoch() -> int:
    if os.environ.get("SOURCE_DATE_EPOCH", "").isdigit():
        return int(os.environ["SOURCE_DATE_EPOCH"])
    try:
        out = subprocess.run(["git", "-C", str(ROOT), "log", "-1", "--format=%ct"], capture_output=True,
                             text=True, check=True).stdout.strip()
        return int(out)
    except (OSError, subprocess.CalledProcessError, ValueError):
        return int(time.time())


def read(path: Path) -> bytes:
    """File contents with LF line endings for text files (Windows checkouts use CRLF)."""
    data = path.read_bytes()
    if path.suffix.lower() in TEXT_SUFFIXES:
        data = data.replace(b"\r\n", b"\n")
    return data


def payload() -> dict[str, tuple[bytes, int]]:
    """Destination path (without leading slash) -> (contents, mode)."""
    files: dict[str, tuple[bytes, int]] = {}

    def add(destination: str, data: bytes, mode: int = 0o644) -> None:
        files[destination] = (data, mode)

    app = "opt/marquee-pi"
    add(f"{app}/marquee_pi.py", read(PI / "marquee_pi.py"))
    add(f"{app}/start-kiosk.sh", read(PI / "start-kiosk.sh"), 0o755)
    add(f"{app}/show-shutdown.sh", read(PI / "show-shutdown.sh"), 0o755)
    for path in sorted((PI / "static").rglob("*")):
        if path.is_file():
            add(f"{app}/static/{path.relative_to(PI / 'static').as_posix()}", read(path))

    systemd = "usr/lib/systemd/system"
    for unit in ("marquee-pi-api", "marquee-pi-kiosk", "marquee-pi-boot-splash",
                 "marquee-pi-shutdown-animation"):
        text = read(PI / f"{unit}.service.example")
        # The kiosk logs in as "pi" by default; postinst adds a drop-in when that user is missing.
        add(f"{systemd}/{unit}.service", text.replace(b"REPLACE_WITH_PI_USER", b"pi"))

    add("usr/share/polkit-1/rules.d/50-marquee-pi.rules", read(PI / "marquee-pi.rules.example"))
    add("etc/chromium/policies/managed/marquee-pi.json", read(PI / "chromium-policy.example.json"))
    add("etc/X11/xorg.conf.d/20-marquee-pi-modesetting.conf", read(PI / "xorg-modesetting.example.conf"))
    add("usr/sbin/marquee-pi-configure-quiet-boot", read(PI / "configure-quiet-boot.sh"), 0o755)
    add("usr/sbin/marquee-display-test", read(PI / "marquee-display-test.sh"), 0o755)

    doc = "usr/share/doc/marquee-pi"
    add(f"{doc}/config.example.json", read(PI / "config.example.json"))
    add(f"{doc}/README.md", read(PI / "README.md"))
    add(f"{doc}/INSTALL.md", read(PI / "INSTALL.md"))
    licence = read(ROOT / "LICENSE").decode("utf-8")
    add(f"{doc}/copyright", (
        f"Upstream: {HOMEPAGE}\n"
        "Files: *\n"
        "The Marquee-Pi contributors license this package under the terms below.\n\n"
        f"{licence}\n"
        "Files: opt/marquee-pi/static/fonts/*\n"
        "Silkscreen, Chakra Petch and IBM Plex Mono are licensed under the SIL Open Font License 1.1;\n"
        "the license texts are installed next to the fonts in /opt/marquee-pi/static/fonts/.\n").encode("utf-8"))
    changelog = gzip.compress(read(ROOT / "CHANGELOG.md"), mtime=0)
    add(f"{doc}/changelog.gz", changelog)
    return files


def tar_gz(entries: list[tuple[str, bytes | None, int]], mtime: int) -> bytes:
    """entries: (name, data or None for a directory, mode). Names start with './'."""
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.GNU_FORMAT) as archive:
        for name, data, mode in entries:
            info = tarfile.TarInfo(name)
            info.uid = info.gid = 0
            info.uname = info.gname = "root"
            info.mtime = mtime
            info.mode = mode
            if data is None:
                info.type = tarfile.DIRTYPE
                archive.addfile(info)
            else:
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
    out = io.BytesIO()
    with gzip.GzipFile(fileobj=out, mode="wb", mtime=0, compresslevel=9) as compressed:
        compressed.write(raw.getvalue())
    return out.getvalue()


def ar_member(name: str, data: bytes, mtime: int, mode: int = 0o100644) -> bytes:
    header = (name.ljust(16) + str(mtime).ljust(12) + "0".ljust(6) + "0".ljust(6) +
              format(mode, "o").ljust(8) + str(len(data)).ljust(10) + "`\n")
    assert len(header) == 60
    body = header.encode("ascii") + data
    return body + (b"\n" if len(data) % 2 else b"")


def build(output: Path, version: str | None = None) -> Path:
    mtime = source_date_epoch()
    version = deb_version(version or app_version())
    files = payload()

    directories = {"."}
    for destination in files:
        parts = destination.split("/")[:-1]
        for index in range(1, len(parts) + 1):
            directories.add("./" + "/".join(parts[:index]))
    data_entries: list[tuple[str, bytes | None, int]] = [
        (name if name == "./" else name + "/", None, 0o755) for name in sorted(directories)]
    data_entries[0] = ("./", None, 0o755)
    for destination in sorted(files):
        data, mode = files[destination]
        data_entries.append(("./" + destination, data, mode))

    installed_kib = -(-sum(len(data) for data, _ in files.values()) // 1024)
    control = "\n".join([
        f"Package: {PACKAGE}",
        f"Version: {version}",
        "Architecture: all",
        f"Maintainer: {MAINTAINER}",
        f"Installed-Size: {installed_kib}",
        f"Depends: {', '.join(DEPENDS)}",
        f"Recommends: {', '.join(RECOMMENDS)}",
        "Section: misc",
        "Priority: optional",
        f"Homepage: {HOMEPAGE}",
        f"Description: {DESCRIPTION[0]}",
        *(" " + (line or ".") for line in DESCRIPTION[1:]),
    ]) + "\n"
    md5sums = "".join(f"{hashlib.md5(files[d][0]).hexdigest()}  {d}\n" for d in sorted(files))

    control_entries: list[tuple[str, bytes | None, int]] = [
        ("./", None, 0o755),
        ("./control", control.encode("utf-8"), 0o644),
        ("./md5sums", md5sums.encode("utf-8"), 0o644),
        ("./conffiles", read(DEBIAN / "conffiles"), 0o644),
    ]
    for script in ("postinst", "prerm", "postrm"):
        control_entries.append((f"./{script}", read(DEBIAN / script), 0o755))

    output.mkdir(parents=True, exist_ok=True)
    target = output / f"{PACKAGE}_{version}_all.deb"
    with target.open("wb") as deb:
        deb.write(b"!<arch>\n")
        deb.write(ar_member("debian-binary", b"2.0\n", mtime))
        deb.write(ar_member("control.tar.gz", tar_gz(control_entries, mtime), mtime))
        deb.write(ar_member("data.tar.gz", tar_gz(data_entries, mtime), mtime))
    return target


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", type=Path, default=ROOT / "dist" / "deb")
    parser.add_argument("--version", help="application version, default: APP_VERSION of pi/marquee_pi.py")
    args = parser.parse_args()
    print(build(args.output, args.version))
    return 0


if __name__ == "__main__":
    sys.exit(main())
