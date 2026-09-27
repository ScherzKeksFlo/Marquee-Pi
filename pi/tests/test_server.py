import base64
import json
from pathlib import Path
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from arcade_pi import DisplayState, Server

PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/lZkAAAAASUVORK5CYII="
)


class StateTests(unittest.TestCase):
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
                with self.assertRaises(HTTPError) as result:
                    urlopen(base + "/v1/status", timeout=3)
                self.assertEqual(result.exception.code, 401)

                upload = Request(
                    base + "/v1/default-media", data=PNG, method="POST",
                    headers={"X-Arcade-Token": "a" * 32, "X-File-Name": "upload.png"},
                )
                with urlopen(upload, timeout=3) as response:
                    uploaded = json.load(response)
                self.assertTrue(uploaded["name"].endswith(".png"))
                self.assertEqual(state.describe()["default_name"], uploaded["name"])
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
                request = Request(base + "/v1/status", headers={"X-Arcade-Token": "a" * 32})
                with urlopen(request, timeout=3) as response:
                    status = json.load(response)
                self.assertEqual(status["game_title"], "Test Game")
                self.assertTrue(status["has_marquee"])

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


if __name__ == "__main__":
    unittest.main()
