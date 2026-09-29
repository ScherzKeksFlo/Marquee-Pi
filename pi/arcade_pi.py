#!/usr/bin/env python3
"""Local display and authenticated control API for Marquee-Pi."""

from __future__ import annotations

import argparse
import base64
import binascii
import hmac
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

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
GESTURES = ("swipe-down", "swipe-up", "swipe-right", "swipe-left", "long-press")
GESTURE_ACTIONS = ("none", "marquee", "box_art", "logo", "controls", "default",
                   "retroarch_menu", "touch_menu")
# The touch menu stays on long press unless a client assigns it elsewhere; older
# clients that only know the four swipes therefore never lose access to it.
DEFAULT_GESTURE_ACTIONS = {kind: "touch_menu" if kind == "long-press" else "none" for kind in GESTURES}
STATIC_MIME = {
    "index.html": "text/html; charset=utf-8",
    "app.js": "text/javascript; charset=utf-8",
    "gesture.js": "text/javascript; charset=utf-8",
    "fallback.svg": "image/svg+xml",
}
MEDIA_CACHE = "public, max-age=31536000, immutable"


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


def verify_mp4(path: Path) -> float | None:
    probe = shutil.which("ffprobe")
    if not probe:
        raise ValueError("ffprobe is required for MP4 uploads")
    result = subprocess.run(
        [probe, "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=codec_name:format=duration", "-of", "json", str(path)],
        capture_output=True, text=True, timeout=15, check=False,
    )
    try:
        details = json.loads(result.stdout)
        codec = details["streams"][0]["codec_name"]
    except (json.JSONDecodeError, KeyError, IndexError, TypeError):
        codec = None
        details = {}
    if result.returncode != 0 or codec != "h264":
        raise ValueError("MP4 must contain an H.264 video stream")
    try:
        duration = float(details["format"]["duration"])
        return duration if math.isfinite(duration) and duration >= 0 else None
    except (KeyError, TypeError, ValueError):
        return None


def unlink_if_exists(path: Path) -> None:
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def process_identity() -> str:
    """Return the pid,start-time,uid tuple expected by recent pkcheck versions."""
    stat = Path("/proc/self/stat").read_text(encoding="ascii")
    fields = stat.rsplit(")", 1)[1].split()
    if len(fields) <= 19:
        raise OSError("Process start time unavailable")
    return f"{os.getpid()},{fields[19]},{os.getuid()}"


MIN_BRIGHTNESS_PERCENT = 5
CLIENT_CONNECTED_SECONDS = 10.0


def local_addresses() -> list[str]:
    """Return the Pi's IPv4 addresses for the status menu; empty when unknown."""
    try:
        result = subprocess.run(["hostname", "-I"], capture_output=True, text=True, errors="replace",
                                timeout=2, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return []
    return [part for part in result.stdout.split() if part.count(".") == 3][:4]


class DisplayState:
    def __init__(self, data_dir: Path, timeout_seconds: int,
                 backlight_root: Path = Path("/sys/class/backlight")):
        self.lock = threading.RLock()
        self.backlight_root = backlight_root
        self.last_client_contact = 0.0
        self.upload_lock = threading.Lock()
        self.data_dir = data_dir
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.timeout_seconds = timeout_seconds
        self.active_file, _ = self._load_media_slot("active.json")
        self.boot_splash_file = self.data_dir / "boot-splash"
        self.shutdown_file, self.shutdown_duration = self._load_media_slot("shutdown.json")
        self.game_title = None
        self.game_media: dict[str, tuple[bytes, str]] = {}
        self.last_heartbeat = 0.0
        self.version = 1
        self.shutting_down = False
        self.instance_id = uuid.uuid4().hex
        self.gesture_actions = self._load_gestures()
        self.gesture_events: list[dict] = []
        self.next_gesture_id = 1
        self._restore_brightness()

    def touch_client(self) -> None:
        self.last_client_contact = time.monotonic()

    def client_connected(self) -> bool:
        contact = self.last_client_contact
        return contact > 0 and time.monotonic() - contact <= CLIENT_CONNECTED_SECONDS

    def _backlight(self) -> Path | None:
        try:
            for candidate in sorted(self.backlight_root.iterdir()):
                if (candidate / "brightness").is_file() and (candidate / "max_brightness").is_file():
                    return candidate
        except OSError:
            pass
        return None

    def brightness(self) -> dict:
        device = self._backlight()
        try:
            maximum = int((device / "max_brightness").read_text().strip())
            raw = int((device / "brightness").read_text().strip())
            if maximum <= 0:
                raise ValueError
        except (AttributeError, OSError, ValueError, TypeError):
            return {"supported": False, "percent": None}
        return {"supported": True, "percent": max(0, min(100, round(raw * 100 / maximum)))}

    def set_brightness(self, percent: int, persist: bool = True) -> int:
        if isinstance(percent, bool) or not isinstance(percent, int):
            raise ValueError("Brightness must be an integer")
        percent = max(MIN_BRIGHTNESS_PERCENT, min(100, percent))
        device = self._backlight()
        if device is None:
            raise ValueError("Brightness control unavailable")
        try:
            maximum = int((device / "max_brightness").read_text().strip())
            (device / "brightness").write_text(str(max(1, round(maximum * percent / 100))))
        except (OSError, ValueError) as exc:
            raise ValueError("Brightness control unavailable") from exc
        if persist:
            pending = self.data_dir / "brightness.json.pending"
            try:
                pending.write_text(json.dumps({"percent": percent}), encoding="utf-8")
                os.replace(pending, self.data_dir / "brightness.json")
            except OSError:
                pass
            finally:
                unlink_if_exists(pending)
        return percent

    def _restore_brightness(self) -> None:
        try:
            saved = json.loads((self.data_dir / "brightness.json").read_text(encoding="utf-8"))
            self.set_brightness(saved["percent"], persist=False)
        except (OSError, ValueError, KeyError, TypeError):
            pass

    def system_info(self) -> dict:
        return {
            "app_version": APP_VERSION,
            "addresses": local_addresses(),
            "client_connected": self.client_connected(),
            "brightness": self.brightness(),
        }

    def _load_media_slot(self, manifest_name: str) -> tuple[Path | None, float | None]:
        try:
            manifest = json.loads((self.data_dir / manifest_name).read_text(encoding="utf-8"))
            name = manifest["name"]
            if Path(name).name != name:
                return None, None
            candidate = self.data_dir / name
            if candidate.is_file() and candidate.suffix.lower() in SUPPORTED:
                validate_media(candidate.read_bytes(), candidate.suffix)
                duration = manifest.get("duration")
                if not isinstance(duration, (int, float)) or not math.isfinite(duration) or duration < 0:
                    duration = None
                return candidate, duration
        except (OSError, ValueError, KeyError, TypeError):
            pass
        return None, None

    def _load_gestures(self) -> dict[str, str]:
        try:
            actions = json.loads((self.data_dir / "gestures.json").read_text(encoding="utf-8"))
            if isinstance(actions, dict):
                return {kind: actions[kind] if actions.get(kind) in GESTURE_ACTIONS
                        else DEFAULT_GESTURE_ACTIONS[kind] for kind in GESTURES}
        except (OSError, ValueError):
            pass
        return DEFAULT_GESTURE_ACTIONS.copy()

    def set_gestures(self, actions: dict) -> None:
        if not isinstance(actions, dict) or any(
            kind not in GESTURES or action not in GESTURE_ACTIONS
            for kind, action in actions.items()
        ):
            raise ValueError("Invalid gesture configuration")
        configured = {kind: actions.get(kind, DEFAULT_GESTURE_ACTIONS[kind]) for kind in GESTURES}
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
                "boot_splash_configured": self.boot_splash_file.is_file(),
                "shutdown_name": self.shutdown_file.name if self.shutdown_file else None,
                "shutdown_video": bool(self.shutdown_file and self.shutdown_file.suffix.lower() == ".mp4"),
                "shutdown_delay": self.shutdown_delay(),
                "shutting_down": self.shutting_down,
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
            self.shutting_down = False
            self.game_title = title
            self.game_media = media
            self.last_heartbeat = time.monotonic()
            self.version += 1

    def show_default(self) -> None:
        with self.lock:
            self.shutting_down = False
            self.game_title = None
            self.game_media = {}
            self.version += 1

    def show_shutdown(self) -> None:
        with self.lock:
            self.shutting_down = True
            self.game_title = None
            self.game_media = {}
            self.version += 1

    def reload(self) -> None:
        active_file, _ = self._load_media_slot("active.json")
        with self.lock:
            self.active_file = active_file
            self.version += 1

    def save_default(self, data: bytes, extension: str) -> str:
        with self.upload_lock:
            return self._save_media_slot(
                data, extension, "active.json", "default", "active_file")

    def _save_media_slot(self, data: bytes, extension: str, manifest_name: str,
                         prefix: str, file_attribute: str,
                         duration_attribute: str | None = None,
                         allowed_extensions=frozenset(SUPPORTED)) -> str:
        extension = extension.lower()
        if extension not in allowed_extensions:
            raise ValueError("Unsupported media type for this slot")
        mime = validate_media(data, extension)
        name = prefix + "-" + uuid.uuid4().hex + extension
        target = self.data_dir / name
        pending = self.data_dir / (name + ".pending")
        manifest = self.data_dir / manifest_name
        pending_manifest = self.data_dir / (manifest_name + ".pending")
        try:
            with pending.open("wb") as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            duration = None
            if mime == "video/mp4":
                duration = verify_mp4(pending)
            os.replace(pending, target)
            manifest_data = {"name": name}
            if duration_attribute and duration is not None:
                manifest_data["duration"] = duration
            with pending_manifest.open("w", encoding="utf-8") as stream:
                json.dump(manifest_data, stream)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(pending_manifest, manifest)
            with self.lock:
                previous = getattr(self, file_attribute)
                setattr(self, file_attribute, target)
                if duration_attribute:
                    setattr(self, duration_attribute, duration)
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

    def save_boot_splash(self, data: bytes, extension: str) -> None:
        with self.upload_lock:
            self._save_boot_splash(data, extension)

    def _save_boot_splash(self, data: bytes, extension: str) -> None:
        extension = extension.lower()
        validate_media(data, extension)
        if extension not in (".jpg", ".jpeg", ".png"):
            raise ValueError("Boot splash must be a PNG or JPEG image")
        pending = self.data_dir / "boot-splash.pending"
        try:
            with pending.open("wb") as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(pending, self.boot_splash_file)
            with self.lock:
                self.version += 1
        finally:
            unlink_if_exists(pending)

    def save_shutdown(self, data: bytes, extension: str) -> str:
        with self.upload_lock:
            return self._save_media_slot(
                data, extension, "shutdown.json", "shutdown", "shutdown_file",
                "shutdown_duration")

    def shutdown_delay(self) -> float:
        with self.lock:
            selected = self.shutdown_file
            duration = self.shutdown_duration
        if not selected or selected.suffix.lower() != ".mp4":
            return 4.0
        if duration is None:
            return 6.0
        return min(30.0, max(4.0, duration + 1.0))


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
    timeout = 15

    def _local(self) -> bool:
        return self.client_address[0] in ("127.0.0.1", "::1")

    def _authorized(self) -> bool:
        provided = self.headers.get("X-Arcade-Token", "")
        allowed = self.server.allowed_clients
        ok = ((not allowed or self.client_address[0] in allowed) and bool(provided) and
              hmac.compare_digest(provided.encode("utf-8"), self.server.token.encode("utf-8")))
        if ok:
            self.server.state.touch_client()
        return ok

    def _power(self, action: str) -> None:
        """Authorise via polkit, then reboot or power off; sends the HTTP response."""
        if not self.server.power_commands_enabled:
            self._json(503, {"error": "Power commands are not configured"})
            return
        policy = "org.freedesktop.login1.reboot" if action == "reboot" else "org.freedesktop.login1.power-off"
        try:
            authorization = subprocess.run(
                ["pkcheck", "--action-id", policy, "--process", process_identity()],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5, check=False)
        except subprocess.TimeoutExpired:
            self._json(503, {"error": "Power authorization timed out"})
            return
        except OSError:
            self._json(503, {"error": "Power authorization unavailable"})
            return
        if authorization.returncode != 0:
            self._json(503, {"error": "Power authorization unavailable"})
            return
        if action == "poweroff":
            self.server.state.show_shutdown()
        delay = self.server.state.shutdown_delay() if action == "poweroff" else 0.5
        threading.Timer(delay, lambda: subprocess.run(
            ["systemctl", action], check=False
        )).start()
        self._json(200, {"ok": True})

    def _send(self, code: int, body: bytes, content_type: str,
              cache_control: str = "no-store", headers: dict | None = None) -> None:
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", cache_control)
        self.send_header("X-Content-Type-Options", "nosniff")
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def _send_file(self, path: Path, content_type: str) -> None:
        size = path.stat().st_size
        start, end = 0, size - 1
        code = 200
        requested = self.headers.get("Range", "")
        if requested:
            try:
                if not requested.startswith("bytes=") or "," in requested:
                    raise ValueError
                first, last = requested[6:].split("-", 1)
                if first:
                    start = int(first)
                    end = int(last) if last else size - 1
                else:
                    suffix = int(last)
                    if suffix <= 0:
                        raise ValueError
                    start = max(0, size - suffix)
                    end = size - 1
                if start < 0 or start >= size or end < start:
                    raise ValueError
                end = min(end, size - 1)
                code = 206
            except (ValueError, TypeError):
                self._send(416, b"", content_type, MEDIA_CACHE,
                           {"Content-Range": f"bytes */{size}", "Accept-Ranges": "bytes"})
                return
        length = max(0, end - start + 1)
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(length))
        self.send_header("Cache-Control", MEDIA_CACHE)
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Accept-Ranges", "bytes")
        if code == 206:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        with path.open("rb") as stream:
            stream.seek(start)
            remaining = length
            while remaining:
                block = stream.read(min(64 * 1024, remaining))
                if not block:
                    break
                self.wfile.write(block)
                remaining -= len(block)

    def _send_media(self, selected: Path | None) -> None:
        if selected:
            try:
                mime = SUPPORTED[selected.suffix.lower()]
                if mime == "video/mp4":
                    self._send_file(selected, mime)
                else:
                    self._send(200, selected.read_bytes(), mime, MEDIA_CACHE)
                return
            except OSError:
                pass
        self._send(200, (STATIC / "fallback.svg").read_bytes(), "image/svg+xml", MEDIA_CACHE)

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
                values = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
                if len(values.get("after", [])) != 1:
                    raise ValueError
                cursor = max(0, int(values["after"][0]))
            except (ValueError, TypeError):
                self._json(400, {"error": "Invalid cursor"})
                return
            self._json(200, self.server.state.events_after(cursor))
            return
        if path == "/ui/state":
            self._json(200, self.server.state.describe())
            return
        if path == "/ui/system":
            self._json(200, {**self.server.state.system_info(),
                             "power_enabled": self.server.power_commands_enabled})
            return
        if path == "/ui/default":
            with self.server.state.lock:
                selected = self.server.state.active_file
            self._send_media(selected)
            return
        if path == "/ui/shutdown":
            with self.server.state.lock:
                selected = self.server.state.shutdown_file
            self._send_media(selected)
            return
        if path.startswith("/ui/game/"):
            kind = path[len("/ui/game/"):]
            with self.server.state.lock:
                media = self.server.state.game_media.get(kind)
            if media:
                self._send(200, media[0], media[1], MEDIA_CACHE)
            else:
                self._json(404, {"error": "No game artwork"})
            return
        if path in ("/ui/", "/ui/index.html", "/ui/app.js", "/ui/gesture.js", "/ui/fallback.svg"):
            filename = "index.html" if path == "/ui/" else path.split("/")[-1]
            self._send(200, (STATIC / filename).read_bytes(), STATIC_MIME[filename], "no-cache")
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
        if path == "/ui/shutdown":
            if not self._local():
                self._json(403, {"error": "Local display only"})
                return
            self.server.state.show_shutdown()
            self._json(200, {"ok": True})
            return
        if path in ("/ui/brightness", "/ui/power"):
            if not self._local():
                self._json(403, {"error": "Local display only"})
                return
            try:
                payload = json.loads(self._body())
                if not isinstance(payload, dict):
                    raise ValueError("Payload must be an object")
                if path == "/ui/brightness":
                    percent = self.server.state.set_brightness(payload.get("percent"))
                    self._json(200, {"ok": True, "percent": percent})
                    return
                action = payload.get("action")
                if action not in ("reboot", "poweroff"):
                    raise ValueError("Invalid power action")
                self._power(action)
            except (ValueError, json.JSONDecodeError) as exc:
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
                warnings = []
                for kind in ("marquee", "controls", "box_art", "logo"):
                    item = payload.get(kind)
                    if not item:
                        continue
                    try:
                        if not isinstance(item, dict):
                            raise ValueError("Artwork must be an object")
                        extension = str(item.get("extension", "")).lower()
                        data = base64.b64decode(item["base64"], validate=True)
                        mime = validate_media(data, extension)
                        if mime == "video/mp4":
                            raise ValueError("Artwork must be an image")
                        media[kind] = (data, mime)
                    except (binascii.Error, KeyError, TypeError, ValueError) as exc:
                        warnings.append({"kind": kind, "error": str(exc) or "Invalid artwork"})
                self.server.state.set_game(title, media)
                self._json(200, {"ok": True, "warnings": warnings})
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
            if path == "/v1/boot-splash":
                filename = Path(self.headers.get("X-File-Name", "")).name
                self.server.state.save_boot_splash(self._body(), Path(filename).suffix.lower())
                self._json(200, {"ok": True})
                return
            if path == "/v1/shutdown-media":
                filename = Path(self.headers.get("X-File-Name", "")).name
                name = self.server.state.save_shutdown(self._body(), Path(filename).suffix.lower())
                self._json(200, {"ok": True, "name": name})
                return
            if path == "/v1/heartbeat":
                self.server.state.heartbeat()
            elif path == "/v1/default":
                self.server.state.show_default()
            elif path == "/v1/reload":
                self.server.state.reload()
            elif path in ("/v1/reboot", "/v1/shutdown"):
                self._power("reboot" if path.endswith("reboot") else "poweroff")
                return
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
