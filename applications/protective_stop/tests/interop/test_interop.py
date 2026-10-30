"""Bond the remote with upstream's own, unmodified machine_app over host loopback.

The automated form of README.md in this directory. The remote's built-in default
peer is 127.0.0.1:8890 with machine_app's id, so no provisioning is needed.
"""

import json
import re
import subprocess
import threading
import time
import urllib.request
from collections.abc import Iterator
from pathlib import Path

import pytest
from twister_harness import DeviceAdapter

PSTOP_C = Path(__file__).resolve().parents[4] / "deps/modules/lib/protective-stop/pstop_c"
MACHINE_PORT = 8890
CONTROL_PLANE = "http://127.0.0.1:8080"
BOND = 0xAD
STOP = 0x92


@pytest.fixture(scope="module")
def machine(tmp_path_factory: pytest.TempPathFactory) -> Iterator[list[str]]:
    build = tmp_path_factory.mktemp("machine")
    subprocess.run(["cmake", "-S", str(PSTOP_C), "-B", str(build)], check=True)
    subprocess.run(["cmake", "--build", str(build), "--target", "machine_app"], check=True)

    proc = subprocess.Popen([str(build / "machine_app"), str(MACHINE_PORT)], stderr=subprocess.PIPE, text=True)
    lines: list[str] = []

    def collect() -> None:
        assert proc.stderr is not None
        for line in proc.stderr:
            lines.append(line)

    threading.Thread(target=collect, daemon=True).start()
    yield lines
    proc.terminate()
    proc.wait(timeout=5)


# machine comes first so it is listening before dut boots the remote.
def test_bonds_with_upstream_machine(machine: list[str], dut: DeviceAdapter) -> None:
    deadline = time.monotonic() + 30
    while True:
        msgs = [int(m) for m in re.findall(r"Got message: (\d+)", "".join(machine))]
        # With no simulated pole closed the remote fails safe, so its heartbeats are STOP.
        if BOND in msgs and msgs[msgs.index(BOND) :].count(STOP) >= 3:
            break
        if time.monotonic() > deadline:
            pytest.fail("no bond and heartbeats from the remote; machine_app said:\n" + "".join(machine[-20:]))
        time.sleep(0.5)

    assert not [line for line in machine if "Invalid request" in line], "".join(machine)

    with urllib.request.urlopen(f"{CONTROL_PLANE}/state.json", timeout=5) as rsp:
        assert json.load(rsp)["pstop_replies"] > 0
