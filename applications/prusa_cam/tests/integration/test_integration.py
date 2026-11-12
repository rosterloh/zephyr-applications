"""End-to-end tests for prusa_cam: shell, web API, MJPEG stream and uploads.

The camera service is a local stand-in (FakeService) that the firmware is built
to reach over plain HTTP; see tests.yaml. On native_sim the firmware runs on
the host and its frames are sim/frame.jpg. On a board (twister
--device-testing, `mise run hil prusa_cam`) the same tests run over the serial
shell and WiFi; see README.md in this directory.
"""

import base64
import json
import os
import re
import socket
import threading
import time
import urllib.error
import urllib.request
from collections.abc import Iterator
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import pytest
from twister_harness import DeviceAdapter, Shell

SERVICE_PORT = 18080  # CONFIG_PRUSA_CAMERA_PORT in tests.yaml
SIM_FRAME = (Path(__file__).parents[2] / "sim/frame.jpg").read_bytes()
TOKEN = "abcdefghij0123456789"
PASSWORD = "integration"
AUTH = {"Authorization": "Basic " + base64.b64encode(f"admin:{PASSWORD}".encode()).decode()}

# The service rejects unknown keys (https://camera-service.prusa3d.com/docs/).
INFO_KEYS = {"config", "features", "device"}
CONFIG_KEYS = {"name", "trigger_scheme", "rotation", "resolution", "network_info"}
DEVICE_KEYS = {"type", "firmware_version", "hardware_version", "manufacturer", "model"}


class FakeService(ThreadingHTTPServer):
    def __init__(self) -> None:
        super().__init__(("0.0.0.0", SERVICE_PORT), Handler)
        self.requests: list[tuple[str, str, dict[str, str], bytes]] = []
        self.status = 204
        self.lock = threading.Lock()

    def wait_for(self, method: str, path: str, timeout: float = 30) -> tuple[dict[str, str], bytes]:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self.lock:
                for m, p, headers, body in self.requests:
                    if (m, p) == (method, path):
                        return headers, body
            time.sleep(0.2)
        pytest.fail(f"no {method} {path}; got {[(m, p) for m, p, _, _ in self.requests]}")

    def clear(self) -> None:
        with self.lock:
            self.requests.clear()


class Handler(BaseHTTPRequestHandler):
    server: FakeService

    def _record(self) -> None:
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        with self.server.lock:
            self.server.requests.append((self.command, self.path, dict(self.headers), body))
        self.send_response(self.server.status)
        self.send_header("Content-Length", "0")
        self.end_headers()

    do_GET = do_PUT = _record

    def log_message(self, *args: object) -> None:
        pass


# service comes first so it is listening before dut boots the firmware.
@pytest.fixture(scope="module")
def service() -> Iterator[FakeService]:
    srv = FakeService()
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    yield srv
    srv.shutdown()
    srv.server_close()


@pytest.fixture(scope="module")
def native(dut: DeviceAdapter) -> bool:
    return dut.device_config.type == "native"


@pytest.fixture(scope="module")
def host(service: FakeService, dut: DeviceAdapter, shell: Shell, native: bool) -> str:
    """Address of the device's web server; joins WiFi first on a board."""
    shell.exec_command(f"prusa set password {PASSWORD}")
    shell.exec_command(f"prusa set token {TOKEN}")
    shell.exec_command("prusa set interval 10")
    if native:
        return "127.0.0.1:8280"

    ssid = os.environ.get("PRUSA_HIL_WIFI_SSID")
    if ssid:
        shell.exec_command("wifi cred delete_all")
        shell.exec_command(f'wifi cred add -s "{ssid}" -k 1 -p "{os.environ["PRUSA_HIL_WIFI_PSK"]}"')
        shell.exec_command("wifi cred auto_connect")
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        out = "\n".join(shell.exec_command("net ipv4"))
        m = re.search(r"\b(?!127\.)(\d+\.\d+\.\d+\.\d+)/", out)
        if m:
            return m.group(1)
        time.sleep(2)
    pytest.fail("no IPv4 address; set PRUSA_HIL_WIFI_SSID/PSK or store a network with `wifi cred add`")


def get(url: str, auth: bool = True, timeout: float = 10) -> tuple[int, dict[str, str], bytes]:
    req = urllib.request.Request(url, headers=AUTH if auth else {})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as rsp:
            return rsp.status, dict(rsp.headers), rsp.read()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read()


def post_json(url: str, body: dict[str, object]) -> tuple[int, dict[str, object]]:
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST", headers=AUTH)
    try:
        with urllib.request.urlopen(req, timeout=10) as rsp:
            return rsp.status, json.load(rsp)
    except urllib.error.HTTPError as e:
        return e.code, {}


def assert_jpeg(data: bytes, native: bool) -> None:
    assert data[:2] == b"\xff\xd8" and data[-2:] == b"\xff\xd9", data[:16].hex()
    if native:
        assert data == SIM_FRAME


def test_boots_and_streams(dut: DeviceAdapter, native: bool) -> None:
    if not native:
        # The board booted while it was being flashed, before the console opened.
        dut.write(b"kernel reboot cold\n")
    dut.readlines_until(regex=r"Streaming \w+ JPEG", timeout=30)


def test_shell_show(shell: Shell, host: str) -> None:
    out = "\n".join(shell.exec_command("prusa show"))
    assert re.search(r"token:\s+set", out)
    assert re.search(r"password:\s+set", out)
    assert re.search(r"interval:\s+10 s", out)
    assert re.search(r"fingerprint:\s+zephyrcam[0-9a-f]+", out)


def test_shell_rejects_bad_value(shell: Shell, host: str) -> None:
    out = "\n".join(shell.exec_command("prusa set rotation 45"))
    assert "bad key or value" in out


def test_index_page(host: str) -> None:
    status, headers, body = get(f"http://{host}/", auth=False)
    assert status == 200
    assert headers.get("Content-Encoding") == "gzip" and body


def test_api_needs_password(host: str) -> None:
    status, _, _ = get(f"http://{host}/api/status", auth=False)
    assert status == 401


def test_config_round_trip(host: str) -> None:
    status, cfg = post_json(f"http://{host}/api/config", {"quality": 55, "rotation": 0, "hflip": True})
    assert status == 200
    assert cfg["quality"] == 55 and cfg["hflip"] is True and cfg["token_set"] is True
    status, _, body = get(f"http://{host}/api/config")
    assert status == 200 and json.loads(body)["quality"] == 55
    assert post_json(f"http://{host}/api/config", {"rotation": 45})[0] == 400


def test_home_assistant(host: str) -> None:
    assert post_json(f"http://{host}/api/config", {"ha_key": "AAECAwQF"})[0] == 400  # not 32 bytes
    key = base64.b64encode(bytes(range(32))).decode()
    status, cfg = post_json(f"http://{host}/api/config", {"ha_key": key})
    assert status == 200 and "ha_key" not in cfg
    # The ESPHome API listens; Noise itself is covered by the esphome_api tests.
    with socket.create_connection((host.split(":")[0], 6053), timeout=10):
        pass


def test_snapshot(host: str, native: bool) -> None:
    status, headers, body = get(f"http://{host}/snapshot.jpg")
    assert status == 200 and headers["Content-Type"].startswith("image/jpeg")
    assert_jpeg(body, native)


def test_mjpeg_stream(host: str, native: bool) -> None:
    stream_host = host.replace(":8280", ":8281") if native else f"{host}:81"
    req = urllib.request.Request(f"http://{stream_host}/stream", headers=AUTH)
    with urllib.request.urlopen(req, timeout=10) as rsp:
        assert rsp.headers["Content-Type"] == "multipart/x-mixed-replace; boundary=frame"
        for _ in range(2):  # two frames: the stream keeps going
            assert rsp.readline().strip() == b"--frame"
            headers = {}
            while (line := rsp.readline().strip()) != b"":
                name, value = line.decode().split(":", 1)
                headers[name.lower()] = value.strip()
            assert headers["content-type"] == "image/jpeg"
            assert_jpeg(rsp.read(int(headers["content-length"])), native)
            rsp.readline()


def test_uploads(service: FakeService, shell: Shell, host: str, native: bool) -> None:
    fingerprint = re.search(r"fingerprint:\s+(\S+)", "\n".join(shell.exec_command("prusa show"))).group(1)
    service.status = 204
    service.clear()
    shell.exec_command("prusa snap")

    headers, body = service.wait_for("PUT", "/v1/camera-info")
    assert headers["Token"] == TOKEN and headers["Fingerprint"] == fingerprint
    assert headers["User-Agent"].startswith("prusa_cam/")
    assert headers["Content-Type"] == "application/json"
    info = json.loads(body)
    assert set(info) <= INFO_KEYS and set(info["config"]) <= CONFIG_KEYS and set(info["device"]) <= DEVICE_KEYS
    assert info["config"]["name"] and info["config"]["trigger_scheme"] == "TEN_SEC"

    # The snapshot follows on the next 10 s tick.
    headers, body = service.wait_for("PUT", "/v1/snapshot", timeout=30)
    assert headers["Token"] == TOKEN and headers["Content-Type"] == "image/jpeg"
    assert int(headers["Content-Length"]) == len(body)
    assert_jpeg(body, native)

    status, _, body = get(f"http://{host}/api/status")
    st = json.loads(body)
    assert status == 200 and st["uploads"] >= 1 and not st["stopped"]


def test_rejected_token_stops_until_test_passes(service: FakeService, shell: Shell, host: str) -> None:
    service.status = 401
    service.clear()
    shell.exec_command("prusa snap")
    service.wait_for("PUT", "/v1/camera-info")
    deadline = time.monotonic() + 10
    while "stopped until the config changes" not in "\n".join(shell.exec_command("prusa show")):
        assert time.monotonic() < deadline, "a 401 did not stop the uploads"
        time.sleep(0.5)

    # Fixed in Connect: a passing connection test resumes.
    service.status = 200
    service.clear()
    shell.exec_command("prusa test")
    service.wait_for("GET", "/v1/camera-info")
    deadline = time.monotonic() + 10
    while "stopped" in "\n".join(shell.exec_command("prusa show")):
        assert time.monotonic() < deadline, "a passing test did not clear the stop"
        time.sleep(0.5)
    # camera-info is still stale, so it goes first, then the snapshot.
    service.wait_for("PUT", "/v1/snapshot", timeout=40)
