import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PI = Path(__file__).resolve().parent.parent
SCRIPT = PI / "start-kiosk.sh"

XRANDR_TWO_SCREENS = """Screen 0: minimum 320 x 200, current 720 x 1280, maximum 16384 x 16384
HDMI-1 connected primary 720x1280+0+0 (normal left inverted right x axis y axis) 530mm x 300mm
   1920x1080     60.00 +
DSI-1 connected 800x480+0+0 (normal left inverted right x axis y axis) 155mm x 86mm
   800x480       60.00*+
Composite-1 disconnected (normal left inverted right x axis y axis)
"""
XRANDR_LEGACY = """Screen 0: minimum 320 x 200, current 800 x 480, maximum 16384 x 16384
Composite-1 connected 720x480+0+0 (normal left inverted right x axis y axis)
DSI-1 connected primary 800x480+0+0 (normal left inverted right x axis y axis) 155mm x 86mm
"""
PROFILE_HDMI_ROTATED = """MP_CONFIGURED=1
MP_OUTPUT=HDMI-1
MP_MODE=1280x720
MP_ROTATE=right
MP_MATRIX='0 1 0 -1 0 1 0 0 1'
MP_SCALE=auto
MP_TOUCH_DEVICE='ILITEK ILITEK-TP'
"""


@unittest.skipUnless(shutil.which("sh"), "needs a POSIX shell")
class KioskScriptTests(unittest.TestCase):
    """Run start-kiosk.sh with stand-ins for curl, xrandr, xinput and the browser and look at what it did."""

    def run_script(self, xrandr_text, profile=None):
        with tempfile.TemporaryDirectory() as directory:
            stubs = Path(directory) / "bin"
            stubs.mkdir()
            log = Path(directory) / "calls.log"
            (Path(directory) / "xrandr.txt").write_text(xrandr_text)
            if profile is not None:
                (Path(directory) / "profile.txt").write_text(profile)

            def stub(name, body):
                path = stubs / name
                path.write_text("#!/bin/sh\n" + body)
                path.chmod(0o755)

            stub("chromium-browser", 'echo "browser $*" >> "%s"' % log.as_posix())
            stub("xset", "exit 0")
            stub("sleep", "exit 0")
            stub("xinput", 'echo "xinput $*" >> "%s"' % log.as_posix())
            stub("xrandr", 'if [ "$1" = "--query" ]; then cat "%s"; else echo "xrandr $*" >> "%s"; fi'
                 % ((Path(directory) / "xrandr.txt").as_posix(), log.as_posix()))
            stub("curl", 'if [ -f "%s" ]; then cat "%s"; else exit 22; fi'
                 % ((Path(directory) / "profile.txt").as_posix(), (Path(directory) / "profile.txt").as_posix()))
            stub("python3", 'exec "%s" "$@"' % Path(sys.executable).as_posix())
            env = dict(os.environ, PATH=str(stubs) + os.pathsep + os.environ["PATH"])
            result = subprocess.run(["sh", str(SCRIPT)], env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            return log.read_text().splitlines() if log.exists() else [], result.stderr

    def test_configured_screen_rotation_touch_matrix_and_scale(self):
        calls, _ = self.run_script(XRANDR_TWO_SCREENS, PROFILE_HDMI_ROTATED)
        self.assertIn("xrandr --output DSI-1 --off", calls)  # the other screen is switched off
        self.assertIn("xrandr --output HDMI-1 --primary --mode 1280x720 --rotate right", calls)
        self.assertIn("xinput set-prop ILITEK ILITEK-TP Coordinate Transformation Matrix 0 1 0 -1 0 1 0 0 1", calls)
        browser = [c for c in calls if c.startswith("browser ")][0]
        self.assertIn("--force-device-scale-factor=1.5", browser)  # short side 720 px / 480
        self.assertTrue(browser.endswith("http://127.0.0.1:8765/ui/"))
        self.assertLess(calls.index("xrandr --output HDMI-1 --primary --mode 1280x720 --rotate right"),
                        calls.index([c for c in calls if c.startswith("xinput")][0]))

    def test_view_only_display_sets_no_touch_matrix(self):
        profile = PROFILE_HDMI_ROTATED.replace("MP_TOUCH_DEVICE='ILITEK ILITEK-TP'", "MP_TOUCH_DEVICE=''")
        calls, _ = self.run_script(XRANDR_TWO_SCREENS, profile)
        self.assertFalse([c for c in calls if c.startswith("xinput")])

    def test_without_a_profile_the_old_dsi_behaviour_stays(self):
        calls, stderr = self.run_script(XRANDR_LEGACY, None)
        self.assertIn("xrandr --output Composite-1 --off --output DSI-1 --primary --auto", calls)
        self.assertNotIn("--force-device-scale-factor", [c for c in calls if c.startswith("browser ")][0])
        self.assertIn("Display profile unavailable", stderr)

    def test_a_screen_that_is_not_connected_leaves_the_outputs_alone(self):
        profile = PROFILE_HDMI_ROTATED.replace("MP_OUTPUT=HDMI-1", "MP_OUTPUT=HDMI-2")
        calls, stderr = self.run_script(XRANDR_TWO_SCREENS, profile)
        self.assertFalse([c for c in calls if c.startswith("xrandr")])
        self.assertIn("HDMI-2 is not connected", stderr)

    def test_an_unsupported_mode_falls_back_to_auto(self):
        # the stub xrandr cannot fail per call, so check the script text instead
        text = SCRIPT.read_text()
        self.assertIn("--mode \"$MP_MODE\" --rotate \"$MP_ROTATE\" \\\n        || xrandr --output \"$MP_OUTPUT\" --primary --auto", text)


class ScriptSyntaxTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("sh"), "needs a POSIX shell")
    def test_shell_scripts_parse(self):
        for name in ("start-kiosk.sh", "marquee-display-test.sh", "marquee-pi-configure-display.sh"):
            result = subprocess.run(["sh", "-n", str(PI / name)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, "%s: %s" % (name, result.stderr))


if __name__ == "__main__":
    unittest.main()
