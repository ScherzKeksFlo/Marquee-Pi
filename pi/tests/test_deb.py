import gzip
import hashlib
import importlib.util
import io
import os
import re
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path

BUILDER = Path(__file__).resolve().parents[2] / "packaging" / "deb" / "build_deb.py"
spec = importlib.util.spec_from_file_location("build_deb", BUILDER)
build_deb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_deb)


def read_deb(path: Path):
    """Return (members in order, control fields/files, data entries) parsed from a .deb."""
    blob = path.read_bytes()
    assert blob.startswith(b"!<arch>\n")
    members, offset = [], 8
    while offset < len(blob):
        header = blob[offset:offset + 60]
        assert header[58:60] == b"`\n"
        name = header[:16].decode().strip()
        size = int(header[48:58])
        members.append((name, blob[offset + 60:offset + 60 + size]))
        offset += 60 + size + (size % 2)
    contents = dict(members)

    def tar_entries(data):
        with tarfile.open(fileobj=io.BytesIO(gzip.decompress(data))) as archive:
            return {member.name: (member, archive.extractfile(member).read() if member.isfile() else None)
                    for member in archive.getmembers()}

    return members, tar_entries(contents["control.tar.gz"]), tar_entries(contents["data.tar.gz"])


class DebPackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        os.environ["SOURCE_DATE_EPOCH"] = "1790000000"
        cls.deb = build_deb.build(Path(cls.directory.name))
        cls.members, cls.control, cls.data = read_deb(cls.deb)
        cls.fields = dict(
            line.split(": ", 1) for line in cls.control["./control"][1].decode().splitlines()
            if re.match(r"^[A-Za-z-]+: ", line))

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()
        os.environ.pop("SOURCE_DATE_EPOCH", None)

    def test_version_uses_tilde_for_prereleases(self):
        self.assertEqual(build_deb.deb_version("1.0.0-beta.1"), "1.0.0~beta.1")
        self.assertEqual(build_deb.deb_version("1.0.0"), "1.0.0")
        self.assertEqual(build_deb.deb_version("1.2.3-rc.2"), "1.2.3~rc.2")
        self.assertEqual(self.fields["Version"], build_deb.deb_version(build_deb.app_version()))
        self.assertEqual(self.deb.name, f"marquee-pi_{self.fields['Version']}_all.deb")

    def test_archive_layout_and_control_fields(self):
        self.assertEqual([name for name, _ in self.members], ["debian-binary", "control.tar.gz", "data.tar.gz"])
        self.assertEqual(dict(self.members)["debian-binary"], b"2.0\n")
        for field in ("Package", "Version", "Architecture", "Maintainer", "Installed-Size", "Depends",
                      "Section", "Priority", "Homepage", "Description"):
            self.assertIn(field, self.fields)
        self.assertEqual(self.fields["Package"], "marquee-pi")
        self.assertEqual(self.fields["Architecture"], "all")
        for dependency in ("python3", "ffmpeg", "fbi", "xinit", "polkitd | policykit-1", "adduser"):
            self.assertIn(dependency, self.fields["Depends"])
        self.assertGreater(int(self.fields["Installed-Size"]), 0)

    def test_everything_is_owned_by_root_with_sane_modes(self):
        for name, (member, _) in list(self.data.items()) + list(self.control.items()):
            self.assertEqual((member.uid, member.gid, member.uname, member.gname), (0, 0, "root", "root"), name)
            self.assertEqual(member.mtime, 1790000000)
        for name in ("./opt/marquee-pi/start-kiosk.sh", "./opt/marquee-pi/show-shutdown.sh",
                     "./usr/sbin/marquee-pi-configure-quiet-boot", "./usr/sbin/marquee-display-test"):
            self.assertEqual(self.data[name][0].mode, 0o755, name)
        self.assertEqual(self.data["./opt/marquee-pi/marquee_pi.py"][0].mode, 0o644)
        for script in ("postinst", "prerm", "postrm"):
            self.assertEqual(self.control[f"./{script}"][0].mode, 0o755)
        for name, (member, _) in self.data.items():
            if member.isdir():
                self.assertEqual(member.mode, 0o755, name)

    def test_expected_files_are_installed(self):
        for name in ("./opt/marquee-pi/marquee_pi.py", "./opt/marquee-pi/static/index.html",
                     "./opt/marquee-pi/static/app.js", "./opt/marquee-pi/static/gesture.js",
                     "./usr/lib/systemd/system/marquee-pi-api.service",
                     "./usr/lib/systemd/system/marquee-pi-kiosk.service",
                     "./usr/lib/systemd/system/marquee-pi-boot-splash.service",
                     "./usr/lib/systemd/system/marquee-pi-shutdown-animation.service",
                     "./usr/share/polkit-1/rules.d/50-marquee-pi.rules",
                     "./usr/share/doc/marquee-pi/config.example.json", "./usr/share/doc/marquee-pi/copyright",
                     "./usr/share/doc/marquee-pi/changelog.gz"):
            self.assertIn(name, self.data)

    def test_conffiles_exist_in_the_payload(self):
        listed = self.control["./conffiles"][1].decode().split()
        self.assertEqual(len(listed), 2)
        for path in listed:
            self.assertIn("." + path, self.data, path)

    def test_md5sums_match_the_payload(self):
        lines = self.control["./md5sums"][1].decode().splitlines()
        self.assertEqual(len(lines), sum(1 for _, (m, _) in self.data.items() if m.isfile()))
        for line in lines:
            digest, path = line.split("  ", 1)
            self.assertEqual(hashlib.md5(self.data["./" + path][1]).hexdigest(), digest, path)

    def test_text_files_use_lf_line_endings(self):
        for source in (self.data, self.control):
            for name, (member, data) in source.items():
                if member.isfile() and not name.endswith(".gz"):
                    self.assertNotIn(b"\r", data, f"{name} contains CR")

    def test_units_point_at_installed_paths(self):
        api = self.data["./usr/lib/systemd/system/marquee-pi-api.service"][1].decode()
        self.assertIn("ExecStart=/usr/bin/python3 /opt/marquee-pi/marquee_pi.py --config /etc/marquee-pi/config.json", api)
        self.assertIn("User=marqueepi", api)
        for unit in ("kiosk", "boot-splash", "shutdown-animation"):
            text = self.data[f"./usr/lib/systemd/system/marquee-pi-{unit}.service"][1].decode()
            for path in re.findall(r"/opt/marquee-pi/[\w./-]+", text):
                self.assertIn("." + path, self.data, f"{unit}: {path}")
        kiosk = self.data["./usr/lib/systemd/system/marquee-pi-kiosk.service"][1].decode()
        self.assertNotIn("REPLACE_WITH_PI_USER", kiosk)
        self.assertIn("User=pi", kiosk)

    def test_no_placeholder_config_is_shipped_as_live_config(self):
        self.assertNotIn("./etc/marquee-pi/config.json", self.data)
        example = self.data["./usr/share/doc/marquee-pi/config.example.json"][1].decode()
        self.assertIn("CHANGE_ME_TO_A_RANDOM_TOKEN_AT_LEAST_24_CHARACTERS", example)
        self.assertIn("CHANGE_ME_TO_A_RANDOM_TOKEN_AT_LEAST_24_CHARACTERS",
                      self.control["./postinst"][1].decode())

    def test_maintainer_scripts_parse_and_use_the_right_names(self):
        for script in ("postinst", "prerm", "postrm"):
            text = self.control[f"./{script}"][1].decode()
            self.assertTrue(text.startswith("#!/bin/sh\n"), script)
            self.assertIn("marquee-pi", text)
            if shutil.which("sh"):
                result = subprocess.run(["sh", "-n"], input=text, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, f"{script}: {result.stderr}")

    def test_old_project_names_are_gone(self):
        pattern = re.compile(rb"arcade[-_ ]?pi|arcadepi|x-arcade", re.I)
        for source in (self.data, self.control):
            for name, (member, data) in source.items():
                if member.isfile() and not name.endswith(".gz"):
                    self.assertIsNone(pattern.search(data), name)

    def test_build_is_reproducible(self):
        with tempfile.TemporaryDirectory() as other:
            again = build_deb.build(Path(other))
            self.assertEqual(again.read_bytes(), self.deb.read_bytes())


if __name__ == "__main__":
    unittest.main()
