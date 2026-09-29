import base64
import json
from pathlib import Path
import tempfile
import threading
import unittest
import http.client
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from arcade_pi import DisplayState, Handler, Server, process_identity

PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/lZkAAAAASUVORK5CYII="
)
MP4 = b"\x00\x00\x00\x18ftypisom" + b"\x00" * 24


class StateTests(unittest.TestCase):
    def test_process_identity_includes_start_time_and_uid(self):
        fake_stat = "123 (marquee pi) S " + " ".join(str(index) for index in range(4, 40))
        with patch("arcade_pi.Path.read_text", return_value=fake_stat), \
             patch("arcade_pi.os.getpid", return_value=123), \
             patch("arcade_pi.os.getuid", return_value=1000, create=True):
            self.assertEqual(process_identity(), "123,22,1000")
        self.assertEqual(Handler.timeout, 15)

    def test_boot_and_shutdown_media_survive_restart(self):
        with tempfile.TemporaryDirectory() as directory:
            data_dir = Path(directory)
            state = DisplayState(data_dir, 60)
            state.save_boot_splash(PNG, ".png")
            shutdown_name = state.save_shutdown(PNG, ".png")
            restored = DisplayState(data_dir, 60)
            self.assertTrue(restored.describe()["boot_splash_configured"])
            self.assertEqual(restored.describe()["shutdown_name"], shutdown_name)
            self.assertFalse(restored.describe()["shutting_down"])
            restored.show_shutdown()
            self.assertTrue(restored.describe()["shutting_down"])
            restored.set_game("New game", {})
            self.assertFalse(restored.describe()["shutting_down"])

    def test_shutdown_duration_is_cached_and_legacy_manifest_is_supported(self):
        with tempfile.TemporaryDirectory() as directory:
            data_dir = Path(directory)
            state = DisplayState(data_dir, 60)
            with patch("arcade_pi.verify_mp4", return_value=8.5) as verify:
                name = state.save_shutdown(MP4, ".mp4")
            verify.assert_called_once()
            manifest_path = data_dir / "shutdown.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(manifest, {"name": name, "duration": 8.5})
            restored = DisplayState(data_dir, 60)
            self.assertEqual(restored.shutdown_delay(), 9.5)
            self.assertEqual(restored.describe()["shutdown_delay"], 9.5)

            manifest_path.write_text(json.dumps({"name": name}), encoding="utf-8")
            legacy = DisplayState(data_dir, 60)
            self.assertEqual(legacy.shutdown_delay(), 6.0)

    def test_boot_and_shutdown_uploads_use_lock_and_ignore_old_file_delete_error(self):
        class CountingLock:
            def __init__(self):
                self.entries = 0
            def __enter__(self):
                self.entries += 1
            def __exit__(self, *_):
                return False

        with tempfile.TemporaryDirectory() as directory:
            data_dir = Path(directory)
            state = DisplayState(data_dir, 60)
            lock = CountingLock()
            state.upload_lock = lock
            state.save_boot_splash(PNG, ".png")
            first = state.save_shutdown(PNG, ".png")

            def fail_only_for_previous(path):
                if path == data_dir / first:
                    raise OSError("simulated delete error")

            with patch("arcade_pi.unlink_if_exists", side_effect=fail_only_for_previous):
                replacement = state.save_shutdown(PNG, ".png")
            self.assertNotEqual(replacement, first)
            self.assertEqual(state.shutdown_file.name, replacement)
            self.assertEqual(lock.entries, 3)

    def test_default_survives_restart_and_bad_upload(self):
        with tempfile.TemporaryDirectory() as directory:
            state = DisplayState(Path(directory), 60)
            name = state.save_default(PNG, ".png")
            self.assertTrue(name.endswith(".png"))
            self.assertEqual(DisplayState(Path(directory), 60).active_file.name, name)
            with self.assertRaises(ValueError):
                state.save_default(b"invalid", ".png")
            self.assertEqual(DisplayState(Path(directory), 60).active_file.name, name)
            replacement = state.save_default(PNG, ".png")
            self.assertNotEqual(replacement, name)
            self.assertFalse((Path(directory) / name).exists())
            self.assertEqual(DisplayState(Path(directory), 60).active_file.name, replacement)

    def test_gesture_configuration_and_retroarch_event(self):
        with tempfile.TemporaryDirectory() as directory:
            state = DisplayState(Path(directory), 60)
            state.set_gestures({"swipe-left": "retroarch_menu",
                                "swipe-right": "box_art"})
            self.assertEqual(DisplayState(Path(directory), 60)
                             .gesture_actions["swipe-right"], "box_art")
            state.set_game("Test", {})
            self.assertTrue(state.record_gesture("swipe-left"))
            self.assertEqual(state.events_after(0)["events"],
                             [{"id": 1, "action": "retroarch_menu"}])
            self.assertEqual(state.events_after(1)["events"], [])
            self.assertFalse(state.record_gesture("swipe-right"))
            state.show_default()
            self.assertFalse(state.record_gesture("swipe-left"))
            with self.assertRaises(ValueError):
                state.set_gestures({"swipe-up": "shutdown"})

    def test_long_press_is_a_configurable_gesture_defaulting_to_touch_menu(self):
        with tempfile.TemporaryDirectory() as directory:
            data_dir = Path(directory)
            state = DisplayState(data_dir, 60)
            self.assertEqual(state.gesture_actions["long-press"], "touch_menu")
            self.assertEqual(state.gesture_actions["swipe-up"], "none")
            # Clients that only know the four swipes must not take the menu away.
            state.set_gestures({"swipe-left": "box_art"})
            self.assertEqual(state.gesture_actions["long-press"], "touch_menu")
            # The menu can move to a swipe, freeing long press for another action.
            state.set_gestures({"swipe-down": "touch_menu", "long-press": "retroarch_menu"})
            restored = DisplayState(data_dir, 60)
            self.assertEqual(restored.gesture_actions["swipe-down"], "touch_menu")
            self.assertEqual(restored.gesture_actions["long-press"], "retroarch_menu")
            restored.set_game("Test", {})
            self.assertTrue(restored.record_gesture("long-press"))
            self.assertEqual(restored.events_after(0)["events"],
                             [{"id": 1, "action": "retroarch_menu"}])
            self.assertFalse(restored.record_gesture("swipe-down"))
            restored.set_gestures({"long-press": "none"})
            self.assertEqual(restored.gesture_actions["long-press"], "none")
            with self.assertRaises(ValueError):
                restored.set_gestures({"double-tap": "touch_menu"})
            (data_dir / "gestures.json").write_text(
                json.dumps({"swipe-left": "logo"}), encoding="utf-8")
            legacy = DisplayState(data_dir, 60)
            self.assertEqual(legacy.gesture_actions["swipe-left"], "logo")
            self.assertEqual(legacy.gesture_actions["long-press"], "touch_menu")

    def test_corrupt_active_file_uses_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            state = DisplayState(Path(directory), 60)
            name = state.save_default(PNG, ".png")
            (Path(directory) / name).write_bytes(b"invalid")
            self.assertIsNone(DisplayState(Path(directory), 60).active_file)


class ApiTests(unittest.TestCase):
    def test_auth_game_and_default(self):
        with tempfile.TemporaryDirectory() as directory:
            state = DisplayState(Path(directory), 60)
            server = Server(("127.0.0.1", 0), state, "a" * 32)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            base = f"http://127.0.0.1:{server.server_port}"
            try:
                with urlopen(base + "/ui/app.js", timeout=3) as response:
                    self.assertEqual(response.headers["Content-Type"], "text/javascript; charset=utf-8")
                    self.assertEqual(response.headers["Cache-Control"], "no-cache")
                with self.assertRaises(HTTPError) as result:
                    urlopen(base + "/v1/status", timeout=3)
                self.assertEqual(result.exception.code, 401)

                connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=3)
                connection.request("GET", "/v1/status", headers={"X-Arcade-Token": "é"})
                self.assertEqual(connection.getresponse().status, 401)
                connection.close()

                upload = Request(
                    base + "/v1/default-media", data=PNG, method="POST",
                    headers={"X-Arcade-Token": "a" * 32, "X-File-Name": "upload.png"},
                )
                with urlopen(upload, timeout=3) as response:
                    uploaded = json.load(response)
                self.assertTrue(uploaded["name"].endswith(".png"))
                self.assertEqual(state.describe()["default_name"], uploaded["name"])
                boot_upload = Request(
                    base + "/v1/boot-splash", data=PNG, method="POST",
                    headers={"X-Arcade-Token": "a" * 32, "X-File-Name": "boot.png"},
                )
                with urlopen(boot_upload, timeout=3) as response:
                    self.assertEqual(response.status, 200)
                shutdown_upload = Request(
                    base + "/v1/shutdown-media", data=PNG, method="POST",
                    headers={"X-Arcade-Token": "a" * 32, "X-File-Name": "shutdown.png"},
                )
                with urlopen(shutdown_upload, timeout=3) as response:
                    self.assertEqual(response.status, 200)
                with urlopen(Request(base + "/ui/shutdown", data=b"", method="POST"), timeout=3):
                    pass
                self.assertTrue(state.describe()["shutting_down"])
                payload = json.dumps({
                    "title": "Test Game",
                    "marquee": {"extension": ".png", "base64": base64.b64encode(PNG).decode()},
                }).encode()
                request = Request(
                    base + "/v1/game", data=payload, method="POST",
                    headers={"X-Arcade-Token": "a" * 32, "Content-Type": "application/json"},
                )
                with urlopen(request, timeout=3) as response:
                    self.assertEqual(response.status, 200)
                with urlopen(base + "/ui/game/marquee?v=1", timeout=3) as response:
                    self.assertEqual(response.headers["Cache-Control"],
                                     "public, max-age=31536000, immutable")
                partial_payload = json.dumps({
                    "title": "Partial Artwork",
                    "marquee": {"extension": ".png", "base64": "not-base64"},
                    "logo": {"extension": ".png", "base64": base64.b64encode(PNG).decode()},
                }).encode()
                partial_request = Request(
                    base + "/v1/game", data=partial_payload, method="POST",
                    headers={"X-Arcade-Token": "a" * 32, "Content-Type": "application/json"},
                )
                with urlopen(partial_request, timeout=3) as response:
                    partial_result = json.load(response)
                self.assertEqual(partial_result["warnings"][0]["kind"], "marquee")
                self.assertFalse(state.describe()["has_marquee"])
                self.assertTrue(state.describe()["has_logo"])
                request = Request(
                    base + "/v1/game", data=payload, method="POST",
                    headers={"X-Arcade-Token": "a" * 32, "Content-Type": "application/json"},
                )
                with urlopen(request, timeout=3):
                    pass
                gestures = Request(
                    base + "/v1/gesture-config",
                    data=json.dumps({"swipe-down": "retroarch_menu",
                                     "swipe-right": "box_art"}).encode(),
                    method="POST",
                    headers={"X-Arcade-Token": "a" * 32,
                             "Content-Type": "application/json"},
                )
                with urlopen(gestures, timeout=3) as response:
                    self.assertEqual(response.status, 200)
                touch = Request(
                    base + "/ui/gesture",
                    data=json.dumps({"kind": "swipe-down"}).encode(),
                    method="POST",
                    headers={"Content-Type": "application/json"},
                )
                with urlopen(touch, timeout=3) as response:
                    self.assertTrue(json.load(response)["queued"])
                event_request = Request(
                    base + "/v1/gesture-events?unused=yes&after=0",
                    headers={"X-Arcade-Token": "a" * 32},
                )
                with urlopen(event_request, timeout=3) as response:
                    events = json.load(response)
                self.assertEqual(events["events"],
                                 [{"id": 1, "action": "retroarch_menu"}])
                power = Request(base + "/v1/shutdown", data=b"", method="POST",
                                headers={"X-Arcade-Token": "a" * 32})
                with self.assertRaises(HTTPError) as result:
                    urlopen(power, timeout=3)
                self.assertEqual(result.exception.code, 503)
                request = Request(base + "/v1/status", headers={"X-Arcade-Token": "a" * 32})
                with urlopen(request, timeout=3) as response:
                    status = json.load(response)
                self.assertEqual(status["game_title"], "Test Game")
                self.assertTrue(status["has_marquee"])

                with patch("arcade_pi.verify_mp4", return_value=8.5):
                    state.save_default(MP4, ".mp4")
                ranged = Request(base + "/ui/default?v=2", headers={"Range": "bytes=4-11"})
                with urlopen(ranged, timeout=3) as response:
                    self.assertEqual(response.status, 206)
                    self.assertEqual(response.headers["Content-Range"],
                                     f"bytes 4-11/{len(MP4)}")
                    self.assertEqual(response.headers["Accept-Ranges"], "bytes")
                    self.assertEqual(response.read(), MP4[4:12])

                request = Request(
                    base + "/v1/default", data=b"", method="POST",
                    headers={"X-Arcade-Token": "a" * 32},
                )
                with urlopen(request, timeout=3):
                    pass
                self.assertIsNone(state.describe()["game_title"])
            finally:
                server.shutdown()
                server.server_close()
                thread.join(timeout=3)


def fake_backlight(root: Path, raw: int = 255, maximum: int = 255) -> Path:
    device = root / "rpi_backlight"
    device.mkdir(parents=True)
    (device / "brightness").write_text(str(raw))
    (device / "max_brightness").write_text(str(maximum))
    return device


class MenuStateTests(unittest.TestCase):
    def test_brightness_is_clamped_scaled_persisted_and_restored(self):
        with tempfile.TemporaryDirectory() as directory:
            data_dir = Path(directory) / "data"
            device = fake_backlight(Path(directory) / "backlight")
            root = device.parent
            state = DisplayState(data_dir, 60, root)
            self.assertEqual(state.brightness(), {"supported": True, "percent": 100})
            self.assertEqual(state.set_brightness(50), 50)
            self.assertEqual((device / "brightness").read_text(), "128")
            self.assertEqual(state.set_brightness(0), 5)
            self.assertEqual((device / "brightness").read_text(), "13")
            self.assertEqual(state.set_brightness(500), 100)
            state.set_brightness(40)
            (device / "brightness").write_text("255")
            DisplayState(data_dir, 60, root)
            self.assertEqual((device / "brightness").read_text(), "102")
            for invalid in ("50", 50.5, True, None):
                with self.assertRaises(ValueError):
                    state.set_brightness(invalid)

    def test_brightness_unavailable_without_backlight(self):
        with tempfile.TemporaryDirectory() as directory:
            state = DisplayState(Path(directory) / "data", 60, Path(directory) / "missing")
            self.assertEqual(state.brightness(), {"supported": False, "percent": None})
            with self.assertRaises(ValueError):
                state.set_brightness(50)

    def test_language_defaults_to_english_persists_and_bumps_version(self):
        with tempfile.TemporaryDirectory() as directory:
            data_dir = Path(directory)
            state = DisplayState(data_dir, 60, data_dir / "missing")
            self.assertEqual(state.describe()["language"], "en")
            version = state.describe()["version"]
            state.set_language("de")
            self.assertEqual(state.describe()["language"], "de")
            self.assertEqual(state.describe()["version"], version + 1)
            state.set_language("de")  # unchanged: clients need not refresh
            self.assertEqual(state.describe()["version"], version + 1)
            self.assertEqual(DisplayState(data_dir, 60, data_dir / "missing").language, "de")
            for invalid in ("fr", "", None, 5):
                with self.assertRaises(ValueError):
                    state.set_language(invalid)
            self.assertEqual(state.language, "de")
            (data_dir / "language.json").write_text('{"language": "xx"}', encoding="utf-8")
            self.assertEqual(DisplayState(data_dir, 60, data_dir / "missing").language, "en")
            (data_dir / "language.json").write_text("not json", encoding="utf-8")
            self.assertEqual(DisplayState(data_dir, 60, data_dir / "missing").language, "en")

    def test_client_connection_expires(self):
        with tempfile.TemporaryDirectory() as directory:
            state = DisplayState(Path(directory), 60, Path(directory) / "missing")
            self.assertFalse(state.client_connected())
            state.touch_client()
            self.assertTrue(state.client_connected())
            state.last_client_contact -= 60
            self.assertFalse(state.client_connected())


class MenuApiTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        root = Path(self.directory.name)
        self.device = fake_backlight(root / "backlight")
        self.state = DisplayState(root / "data", 60, root / "backlight")
        self.server = Server(("127.0.0.1", 0), self.state, "a" * 32, power_commands_enabled=True)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=3)
        self.directory.cleanup()

    def post(self, path, payload):
        request = Request(self.base + path, data=json.dumps(payload).encode(), method="POST")
        return urlopen(request, timeout=3)

    def test_system_status_reports_connection_and_brightness(self):
        with patch("arcade_pi.local_addresses", return_value=["192.168.99.149"]):
            with urlopen(self.base + "/ui/system", timeout=3) as response:
                info = json.load(response)
            self.assertFalse(info["client_connected"])
            self.assertEqual(info["addresses"], ["192.168.99.149"])
            self.assertEqual(info["brightness"], {"supported": True, "percent": 100})
            self.assertTrue(info["power_enabled"])
            urlopen(Request(self.base + "/v1/status", headers={"X-Arcade-Token": "a" * 32}),
                    timeout=3).close()
            with urlopen(self.base + "/ui/system", timeout=3) as response:
                self.assertTrue(json.load(response)["client_connected"])

    def test_language_endpoint_requires_token_and_valid_value(self):
        def post(payload, token="a" * 32):
            request = Request(self.base + "/v1/language", data=json.dumps(payload).encode(), method="POST",
                              headers={"X-Arcade-Token": token})
            return urlopen(request, timeout=3)
        with self.assertRaises(HTTPError) as result:
            post({"language": "de"}, token="wrong")
        self.assertEqual(result.exception.code, 401)
        for payload in ({"language": "fr"}, {}, [], {"language": 1}):
            with self.assertRaises(HTTPError) as result:
                post(payload)
            self.assertEqual(result.exception.code, 400)
        self.assertEqual(self.state.language, "en")
        with post({"language": "de"}) as response:
            self.assertEqual(response.status, 200)
        with urlopen(self.base + "/ui/state", timeout=3) as response:
            self.assertEqual(json.load(response)["language"], "de")

    def test_unauthorized_request_does_not_count_as_client_contact(self):
        with self.assertRaises(HTTPError):
            urlopen(Request(self.base + "/v1/status", headers={"X-Arcade-Token": "wrong"}), timeout=3)
        self.assertFalse(self.state.client_connected())

    def test_brightness_endpoint_validates_input(self):
        with self.post("/ui/brightness", {"percent": 60}) as response:
            self.assertEqual(json.load(response)["percent"], 60)
        self.assertEqual((self.device / "brightness").read_text(), "153")
        for payload in ({"percent": "60"}, {}, []):
            with self.assertRaises(HTTPError) as result:
                self.post("/ui/brightness", payload)
            self.assertEqual(result.exception.code, 400)

    def test_menu_power_requires_valid_action_and_polkit(self):
        with self.assertRaises(HTTPError) as result:
            self.post("/ui/power", {"action": "halt"})
        self.assertEqual(result.exception.code, 400)
        with patch("arcade_pi.process_identity", return_value="1,2,3"), \
             patch("arcade_pi.subprocess.run") as run:
            run.return_value.returncode = 1
            with self.assertRaises(HTTPError) as result:
                self.post("/ui/power", {"action": "reboot"})
            self.assertEqual(result.exception.code, 503)
            self.assertEqual(run.call_count, 1)

    def test_menu_reboot_and_poweroff_use_systemctl(self):
        for action, expected_delay in (("reboot", 0.5), ("poweroff", self.state.shutdown_delay())):
            with patch("arcade_pi.process_identity", return_value="1,2,3"), \
                 patch("arcade_pi.subprocess.run") as run, \
                 patch("arcade_pi.threading.Timer") as timer:
                run.return_value.returncode = 0
                with self.post("/ui/power", {"action": action}) as response:
                    self.assertEqual(response.status, 200)
                self.assertEqual(timer.call_args.args[0], expected_delay)
                timer.return_value.start.assert_called_once()
                timer.call_args.args[1]()
                self.assertEqual(run.call_args.args[0], ["systemctl", action])
        self.assertTrue(self.state.describe()["shutting_down"])

    def test_menu_power_reports_missing_pkcheck_as_unavailable(self):
        with patch("arcade_pi.process_identity", return_value="1,2,3"), \
             patch("arcade_pi.subprocess.run", side_effect=FileNotFoundError):
            with self.assertRaises(HTTPError) as result:
                self.post("/ui/power", {"action": "poweroff"})
        self.assertEqual(result.exception.code, 503)
        self.assertFalse(self.state.describe()["shutting_down"])

    def test_menu_power_disabled_when_not_configured(self):
        self.server.power_commands_enabled = False
        with self.assertRaises(HTTPError) as result:
            self.post("/ui/power", {"action": "reboot"})
        self.assertEqual(result.exception.code, 503)


if __name__ == "__main__":
    unittest.main()
