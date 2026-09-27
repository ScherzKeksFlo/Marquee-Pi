#!/usr/bin/env python3
"""Local display and authenticated control API for Marquee-Pi."""

from __future__ import annotations

import argparse
import base64
import binascii
import hmac
import json
import mimetypes
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

APP_VERSION = "0.1.0"
MAX_REQUEST_BYTES = 32 * 1024 * 1024
MAX_MEDIA_BYTES = 20 * 1024 * 1024
SUPPORTED = {
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
    ".png": "image/png",
    ".gif": "image/gif",
    ".webp": "image/webp",
    ".mp4": "video/mp4",
}
ROOT = Path(__file__).resolve().parent
STATIC = ROOT / "static"
GESTURES = ("swipe-down", "swipe-up", "swipe-right", "swipe-left")
GESTURE_ACTIONS = ("none", "marquee", "box_art", "logo", "controls", "default", "retroarch_menu")


def validate_media(data: bytes, extension: str) -> str:
    """Return the MIME type only for supported file contents."""
    extension = extension.lower()
    if not data or len(data) > MAX_MEDIA_BYTES or extension not in SUPPORTED:
        raise ValueError("Unsupported or oversized media file")
    signatures = {
        ".jpg": data.startswith(b"\xff\xd8\xff"),
        ".jpeg": data.startswith(b"\xff\xd8\xff"),
        ".png": data.startswith(b"\x89PNG\r\n\x1a\n"),
        ".gif": data.startswith((b"GIF87a", b"GIF89a")),
        ".webp": data.startswith(b"RIFF") and data[8:12] == b"WEBP",
        ".mp4": len(data) >= 12 and data[4:8] == b"ftyp",
    }
    if not signatures[extension]:
        raise ValueError("File contents do not match extension")
    return SUPPORTED[extension]


def verify_mp4(path: Path) -> None:
    probe = shutil.which("ffprobe")
    if not probe:
        raise ValueError("ffprobe is required for MP4 uploads")
    result = subprocess.run(
        [probe, "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=codec_name", "-of", "default=nw=1:nk=1", str(path)],
        capture_output=True, text=True, timeout=15, check=False,
    )
    if result.returncode != 0 or result.stdout.strip() != "h264":
        raise ValueError("MP4 must contain an H.264 video stream")


def unlink_if_exists(path: Path) -> None:
    try:
        path.unlink()
    except FileNotFoundError:
        pass


class DisplayState:
    def __init__(self, data_dir: Path, timeout_seconds: int):
        self.lock = threading.RLock()
        self.upload_lock = threading.Lock()
        self.data_dir = data_dir
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.timeout_seconds = timeout_seconds
        self.active_file = self._load_active_file()
        self.game_title = None
        self.game_media: dict[str, tuple[bytes, str]] = {}
        self.last_heartbeat = 0.0
        self.version = 1
        self.instance_id = uuid.uuid4().hex
        self.gesture_actions = self._load_gestures()
        self.gesture_events: list[dict] = []
        self.next_gesture_id = 1

    def _load_active_file(self) -> Path | None:
        manifest = self.data_dir / "active.json"
        try:
            name = json.loads(manifest.read_text(encoding="utf-8"))["name"]
            if Path(name).name != name:
                return None
            candidate = self.data_dir / name
            if candidate.is_file() and candidate.suffix.lower() in SUPPORTED:
                validate_media(candidate.read_bytes(), candidate.suffix)
                return candidate
        except (OSError, ValueError, KeyError, TypeError):
            pass
        return None

    def _load_gestures(self) -> dict[str, str]:
        try:
            actions = json.loads((self.data_dir / "gestures.json").read_text(encoding="utf-8"))
            if isinstance(actions, dict):
                return {kind: actions.get(kind, "none") if actions.get(kind) in GESTURE_ACTIONS
                        else "none" for kind in GESTURES}
        except (OSError, ValueError):
            pass
        return {kind: "none" for kind in GESTURES}

    def set_gestures(self, actions: dict) -> None:
        if not isinstance(actions, dict) or any(
            kind not in GESTURES or action not in GESTURE_ACTIONS
            for kind, action in actions.items()
        ):
            raise ValueError("Invalid gesture configuration")
        configured = {kind: actions.get(kind, "none") for kind in GESTURES}
        pending = self.data_dir / "gestures.json.pending"
        with self.upload_lock:
            try:
                with pending.open("w", encoding="utf-8") as stream:
                    json.dump(configured, stream)
                    stream.flush()
                    os.fsync(stream.fileno())
                os.replace(pending, self.data_dir / "gestures.json")
                with self.lock:
                    self.gesture_actions = configured
                    self.version += 1
            finally:
                unlink_if_exists(pending)

    def record_gesture(self, kind: str) -> bool:
        if kind not in GESTURES:
            raise ValueError("Invalid gesture")
        with self.lock:
            self._expire_game()
            if self.gesture_actions[kind] != "retroarch_menu" or not self.game_title:
                return False
            event = {"id": self.next_gesture_id, "action": "retroarch_menu"}
            self.next_gesture_id += 1
            self.gesture_events.append(event)
            self.gesture_events = self.gesture_events[-100:]
            return True

    def events_after(self, cursor: int) -> dict:
        with self.lock:
            return {
                "instance_id": self.instance_id,
                "events": [event for event in self.gesture_events if event["id"] > cursor][:20],
            }

    def describe(self) -> dict:
        with self.lock:
            self._expire_game()
            return {
                "version": self.version,
                "game_title": self.game_title,
                "has_marquee": "marquee" in self.game_media,
                "has_controls": "controls" in self.game_media,
                "has_box_art": "box_art" in self.game_media,
                "has_logo": "logo" in self.game_media,
                "gesture_actions": self.gesture_actions.copy(),
                "instance_id": self.instance_id,
                "default_name": self.active_file.name if self.active_file else None,
                "default_video": bool(self.active_file and self.active_file.suffix.lower() == ".mp4"),
            }

    def _expire_game(self) -> None:
        if self.game_title and time.monotonic() - self.last_heartbeat > self.timeout_seconds:
            self.game_title = None
            self.game_media = {}
            self.version += 1

    def heartbeat(self) -> None:
        with self.lock:
            self.last_heartbeat = time.monotonic()

    def set_game(self, title: str, media: dict[str, tuple[bytes, str]]) -> None:
        with self.lock:
            self.game_title = title
            self.game_media = media
            self.last_heartbeat = time.monotonic()
            self.version += 1

    def show_default(self) -> None:
        with self.lock:
            self.game_title = None
            self.game_media = {}
            self.version += 1

    def reload(self) -> None:
        with self.lock:
            self.active_file = self._load_active_file()
            self.version += 1

    def save_default(self, data: bytes, extension: str) -> str:
        with self.upload_lock:
            return self._save_default_impl(data, extension)

    def _save_default_impl(self, data: bytes, extension: str) -> str:
        mime = validate_media(data, extension)
        name = "default-" + uuid.uuid4().hex + extension.lower()
        target = self.data_dir / name
        pending = self.data_dir / (name + ".pending")
        manifest = self.data_dir / "active.json"
        pending_manifest = self.data_dir / "active.json.pending"
        try:
            with pending.open("wb") as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            if mime == "video/mp4":
                verify_mp4(pending)
            os.replace(pending, target)
            with pending_manifest.open("w", encoding="utf-8") as stream:
                json.dump({"name": name}, stream)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(pending_manifest, manifest)
            with self.lock:
                previous = self.active_file
                self.active_file = target
                self.version += 1
            if previous and previous != target:
                try:
                    unlink_if_exists(previous)
                except OSError:
                    pass
            return name
        finally:
            unlink_if_exists(pending)
            unlink_if_exists(pending_manifest)


class Server(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, state: DisplayState, token: str, allowed_clients=None, power_commands_enabled=False):
        super().__init__(address, Handler)
        self.state = state
        self.token = token
        self.allowed_clients = set(allowed_clients or [])
        self.power_commands_enabled = power_commands_enabled


class Handler(BaseHTTPRequestHandler):
    server: Server

    def _local(self) -> bool:
        return self.client_address[0] in ("127.0.0.1", "::1")

    def _authorized(self) -> bool:
        provided = self.headers.get("X-Arcade-Token", "")
        allowed = self.server.allowed_clients
        return (not allowed or self.client_address[0] in allowed) and bool(provided) and hmac.compare_digest(provided, self.server.token)

    def _send(self, code: int, body: bytes, content_type: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, code: int, payload: dict) -> None:
        self._send(code, json.dumps(payload).encode("utf-8"), "application/json; charset=utf-8")

    def _body(self) -> bytes:
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError as exc:
            raise ValueError("Invalid Content-Length") from exc
        if length <= 0 or length > MAX_REQUEST_BYTES:
            raise ValueError("Invalid request size")
        body = self.rfile.read(length)
        if len(body) != length:
            raise ValueError("Incomplete request")
        return body

    def do_GET(self) -> None:
        path = urlsplit(self.path).path
        if path.startswith("/ui/") and not self._local():
            self._json(403, {"error": "Local display only"})
            return
        if path == "/v1/status":
            if not self._authorized():
                self._json(401, {"error": "Unauthorized"})
                return
            self._json(200, {"app_version": APP_VERSION, **self.server.state.describe()})
            return
        if path == "/v1/gesture-events":
            if not self._authorized():
                self._json(401, {"error": "Unauthorized"})
                return
            try:
                cursor = max(0, int(urlsplit(self.path).query.removeprefix("after=")))
            except ValueError:
                self._json(400, {"error": "Invalid cursor"})
                return
            self._json(200, self.server.state.events_after(cursor))
            return
        if path == "/ui/state":
            self._json(200, self.server.state.describe())
            return
        if path == "/ui/default":
            with self.server.state.lock:
                selected = self.server.state.active_file
                if selected:
                    try:
                        data = selected.read_bytes()
                        mime = SUPPORTED[selected.suffix.lower()]
                    except OSError:
                        selected = None
                if not selected:
                    data = (STATIC / "fallback.svg").read_bytes()
                    mime = "image/svg+xml"
            self._send(200, data, mime)
            return
        if path.startswith("/ui/game/"):
            kind = path[len("/ui/game/"):]
            with self.server.state.lock:
                media = self.server.state.game_media.get(kind)
            if media:
                self._send(200, media[0], media[1])
            else:
                self._json(404, {"error": "No game artwork"})
            return
        if path in ("/ui/", "/ui/index.html", "/ui/app.js", "/ui/gesture.js", "/ui/fallback.svg"):
            filename = "index.html" if path == "/ui/" else path.split("/")[-1]
            mime = mimetypes.guess_type(filename)[0] or "application/octet-stream"
            self._send(200, (STATIC / filename).read_bytes(), mime)
            return
        self._json(404, {"error": "Not found"})

    def do_POST(self) -> None:
        path = urlsplit(self.path).path
        if path == "/ui/gesture":
            if not self._local():
                self._json(403, {"error": "Local display only"})
                return
            try:
                payload = json.loads(self._body())
                queued = self.server.state.record_gesture(payload.get("kind", ""))
                self._json(200, {"queued": queued})
            except (ValueError, AttributeError, json.JSONDecodeError) as exc:
                self._json(400, {"error": str(exc)})
            return
        if not path.startswith("/v1/") or not self._authorized():
            self._json(401, {"error": "Unauthorized"})
            return
        try:
            if path == "/v1/game":
                payload = json.loads(self._body())
                if not isinstance(payload, dict):
                    raise ValueError("Game payload must be an object")
                title = str(payload.get("title", "")).strip()[:250]
                if not title:
                    raise ValueError("Game title is required")
                media = {}
                for kind in ("marquee", "controls", "box_art", "logo"):
                    item = payload.get(kind)
                    if not item:
                        continue
                    if not isinstance(item, dict):
                        raise ValueError("Game artwork must be an object")
                    extension = str(item.get("extension", "")).lower()
                    try:
                        data = base64.b64decode(item["base64"], validate=True)
                    except (binascii.Error, KeyError, TypeError) as exc:
                        raise ValueError("Invalid game artwork") from exc
                    mime = validate_media(data, extension)
                    if mime == "video/mp4":
                        raise ValueError("Game artwork must be an image")
                    media[kind] = (data, mime)
                self.server.state.set_game(title, media)
                self._json(200, {"ok": True})
                return
            if path == "/v1/gesture-config":
                payload = json.loads(self._body())
                self.server.state.set_gestures(payload)
                self._json(200, {"ok": True})
                return
            if path == "/v1/default-media":
                filename = Path(self.headers.get("X-File-Name", "")).name
                extension = Path(filename).suffix.lower()
                name = self.server.state.save_default(self._body(), extension)
                self._json(200, {"ok": True, "name": name})
                return
            if path == "/v1/heartbeat":
                self.server.state.heartbeat()
            elif path == "/v1/default":
                self.server.state.show_default()
            elif path == "/v1/reload":
                self.server.state.reload()
            elif path in ("/v1/reboot", "/v1/shutdown"):
                if not self.server.power_commands_enabled:
                    self._json(503, {"error": "Power commands are not configured"})
                    return
                action = "reboot" if path.endswith("reboot") else "poweroff"
                policy = "org.freedesktop.login1.reboot" if action == "reboot" else "org.freedesktop.login1.power-off"
                authorization = subprocess.run(
                    ["pkcheck", "--action-id", policy, "--process", str(os.getpid())],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5, check=False)
                if authorization.returncode != 0:
                    self._json(503, {"error": "Power authorization unavailable"})
                    return
                threading.Timer(0.5, lambda: subprocess.run(
                    ["systemctl", action], check=False
                )).start()
            else:
                self._json(404, {"error": "Not found"})
                return
            self._json(200, {"ok": True})
        except (ValueError, json.JSONDecodeError) as exc:
            self._json(400, {"error": str(exc)})
        except subprocess.TimeoutExpired:
            self._json(503, {"error": "Power authorization timed out"})
        except OSError:
            self._json(500, {"error": "Media storage failed"})

    def log_message(self, format: str, *args) -> None:
        request_line = str(args[0]) if args else ""
        if request_line.startswith(("GET /ui/state ", "GET /v1/status ", "GET /v1/gesture-events?", "POST /v1/heartbeat ")):
            return
        print("%s %s" % (self.address_string(), format % args), flush=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, required=True)
    args = parser.parse_args()
    config = json.loads(args.config.read_text(encoding="utf-8"))
    token = config["token"]
    if len(token) < 24 or token.startswith("CHANGE"):
        raise SystemExit("Set a random token of at least 24 characters")
    data_dir = Path(config["data_dir"]).expanduser().resolve()
    state = DisplayState(data_dir, int(config.get("game_timeout_seconds", 60)))
    server = Server((config.get("bind", "0.0.0.0"), int(config.get("port", 8765))), state, token,
                    config.get("allowed_client_ips", []), bool(config.get("power_commands_enabled", False)))
    print("Marquee-Pi listening on %s:%s" % server.server_address, flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
