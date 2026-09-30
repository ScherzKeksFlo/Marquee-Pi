import http.client
import io
import json
import shlex
import tempfile
import threading
import unittest
from contextlib import redirect_stdout
from pathlib import Path

import display_profile
from display_profile import auto_scale, parse_profile, profile_to_config, shell_env
from marquee_pi import DisplayState, Server

TOKEN = "a" * 32


def read_env(text):
    """What `. file` would set in a POSIX shell."""
    values = {}
    for line in text.splitlines():
        (assignment,) = shlex.split(line)
        key, value = assignment.split("=", 1)
        values[key] = value
    return values


class ParseTests(unittest.TestCase):
    def test_absent_section_keeps_the_old_behaviour(self):
        profile, warnings = parse_profile(None)
        self.assertEqual(warnings, [])
        self.assertFalse(profile["configured"])
        self.assertTrue(profile["touch"])  # touch was always assumed before display profiles
        self.assertEqual((profile["mode"], profile["rotation"], profile["fit"], profile["scale"]),
                         ("auto", 0, "contain", "auto"))

    def test_full_profile(self):
        profile, warnings = parse_profile({
            "output": "HDMI-1", "mode": "1920x1080", "rotation": 90, "fit": "cover", "scale": 1.5,
            "touch": {"device": "ILITEK ILITEK-TP"}, "backlight": "10-0045"})
        self.assertEqual(warnings, [])
        self.assertTrue(profile["configured"])
        self.assertEqual((profile["output"], profile["mode"], profile["rotation"], profile["fit"], profile["scale"]),
                         ("HDMI-1", "1920x1080", 90, "cover", 1.5))
        self.assertEqual((profile["touch"], profile["touch_device"], profile["backlight"]),
                         (True, "ILITEK ILITEK-TP", "10-0045"))

    def test_view_only_needs_an_explicit_null_or_false(self):
        self.assertFalse(parse_profile({"touch": None})[0]["touch"])
        self.assertFalse(parse_profile({"touch": False})[0]["touch"])
        self.assertTrue(parse_profile({"rotation": 90})[0]["touch"])  # key missing: assumed
        profile, warnings = parse_profile({"touch": "yes"})
        self.assertTrue(profile["touch"])
        self.assertEqual(len(warnings), 1)

    def test_wrong_values_fall_back_with_a_warning_each(self):
        profile, warnings = parse_profile({
            "output": "bad name;rm", "mode": "big", "rotation": 45, "fit": "stretch", "scale": 9,
            "backlight": "../x"})
        self.assertEqual(len(warnings), 6)
        self.assertEqual((profile["output"], profile["mode"], profile["rotation"], profile["fit"],
                          profile["scale"], profile["backlight"]), (None, "auto", 0, "contain", "auto", None))
        self.assertEqual(len(parse_profile({"rotation": True})[1]), 1)  # booleans are not numbers
        self.assertEqual(len(parse_profile([1])[1]), 1)

    def test_round_trip_through_the_config_form(self):
        for raw in ({"output": "DSI-1", "mode": "auto", "rotation": 0, "fit": "contain", "scale": "auto",
                     "touch": {"device": "generic ft5x06 (79)"}},
                    {"output": None, "mode": "800x480", "rotation": 270, "fit": "cover", "scale": 2.0,
                     "touch": None, "backlight": "rpi_backlight"}):
            profile, warnings = parse_profile(raw)
            self.assertEqual(warnings, [])
            self.assertEqual(profile_to_config(profile), raw)

    def test_auto_scale_makes_the_short_side_480_css_pixels(self):
        self.assertEqual(auto_scale(800, 480), 1.0)
        self.assertEqual(auto_scale(1920, 1080), 2.25)
        self.assertEqual(auto_scale(1080, 1920), 2.25)  # rotated: the short side is the same
        self.assertEqual(auto_scale(1920, 360), 0.75)
        self.assertEqual(auto_scale(3840, 2160), 4.0)  # limited
        self.assertEqual(auto_scale(320, 200), 0.5)

    def test_shell_environment(self):
        profile, _ = parse_profile({"output": "HDMI-1", "mode": "1280x720", "rotation": 270, "scale": 1.25,
                                    "touch": {"device": "Touch O'Brien"}})
        env = read_env(shell_env(profile))
        self.assertEqual(env["MP_OUTPUT"], "HDMI-1")
        self.assertEqual(env["MP_MODE"], "1280x720")
        self.assertEqual(env["MP_ROTATE"], "left")
        self.assertEqual(env["MP_MATRIX"], "0 -1 1 1 0 0 0 0 1")
        self.assertEqual(env["MP_SCALE"], "1.25")
        self.assertEqual(env["MP_TOUCH_DEVICE"], "Touch O'Brien")  # quoting survives a name with a quote
        auto, _ = parse_profile({"touch": None})
        env = read_env(shell_env(auto))
        self.assertEqual((env["MP_MODE"], env["MP_SCALE"], env["MP_TOUCH_DEVICE"], env["MP_ROTATE"]),
                         ("", "auto", "", "normal"))

    def test_env_command_reads_a_config_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            path.write_text(json.dumps({"display": {"output": "DSI-1", "rotation": 180}}), encoding="utf-8")
            out = io.StringIO()
            with redirect_stdout(out):
                self.assertEqual(display_profile.main(["display_profile.py", "--env", str(path)]), 0)
            self.assertIn("MP_OUTPUT=DSI-1", out.getvalue())
            self.assertIn("MP_ROTATE=inverted", out.getvalue())
            out = io.StringIO()
            with redirect_stdout(out):  # unreadable file: defaults, never an error
                self.assertEqual(display_profile.main(["display_profile.py", "--env", str(path) + ".missing"]), 0)
            self.assertIn("MP_CONFIGURED=0", out.getvalue())


class StateTests(unittest.TestCase):
    def test_display_info_and_reported_size(self):
        with tempfile.TemporaryDirectory() as directory:
            profile, _ = parse_profile({"output": "HDMI-1", "rotation": 90, "fit": "cover", "touch": None})
            state = DisplayState(Path(directory), 60, display=profile)
            info = state.describe()["display"]
            self.assertEqual((info["output"], info["rotation"], info["fit"], info["touch"]), ("HDMI-1", 90, "cover", False))
            self.assertEqual((info["width"], info["height"], info["test_pattern"]), (None, None, False))
            state.set_display_size(1080, 1920)
            info = state.describe()["display"]
            self.assertEqual((info["width"], info["height"]), (1080, 1920))
            for bad in ((10, 480), (800, None), (True, 480), (800.5, 480), (20000, 480)):
                with self.assertRaises(ValueError):
                    state.set_display_size(*bad)
            (Path(directory) / "display-test").touch()
            self.assertTrue(state.describe()["display"]["test_pattern"])

    def test_old_configuration_assumes_touch(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertTrue(DisplayState(Path(directory), 60).describe()["display"]["touch"])

    def test_backlight_can_be_chosen_by_name(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "backlight"
            for name in ("a_panel", "z_panel"):
                (root / name).mkdir(parents=True)
                (root / name / "max_brightness").write_text("100")
                (root / name / "brightness").write_text("50" if name == "a_panel" else "80")
            data = Path(directory) / "data"
            first = DisplayState(data, 60, backlight_root=root)
            self.assertEqual(first.brightness()["percent"], 50)  # first device, as before
            profile, _ = parse_profile({"backlight": "z_panel"})
            chosen = DisplayState(data, 60, backlight_root=root, display=profile)
            self.assertEqual(chosen.brightness()["percent"], 80)
            profile, _ = parse_profile({"backlight": "missing"})
            self.assertFalse(DisplayState(data, 60, backlight_root=root, display=profile).brightness()["supported"])


class ApiTests(unittest.TestCase):
    def test_display_size_endpoint_is_local_and_validated(self):
        with tempfile.TemporaryDirectory() as directory:
            state = DisplayState(Path(directory), 60)
            server = Server(("127.0.0.1", 0), state, TOKEN)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                def post(body, path="/ui/display"):
                    connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=3)
                    connection.request("POST", path, body=json.dumps(body), headers={"Content-Type": "application/json"})
                    response = connection.getresponse()
                    return response.status, response.read()

                self.assertEqual(post({"width": 1920, "height": 1080})[0], 200)
                self.assertEqual(state.display_size, (1920, 1080))
                self.assertEqual(post({"width": 5, "height": 1080})[0], 400)
                self.assertEqual(post([1, 2])[0], 400)
                connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=3)
                connection.request("GET", "/v1/status", headers={"X-Marquee-Token": TOKEN})
                status = json.loads(connection.getresponse().read())
                self.assertEqual((status["display"]["width"], status["display"]["height"]), (1920, 1080))
                self.assertTrue(status["display"]["touch"])
            finally:
                server.shutdown()
                server.server_close()


if __name__ == "__main__":
    unittest.main()
