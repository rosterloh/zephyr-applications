import struct
import subprocess
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
RASPROVER_SRC = REPO_ROOT / "applications" / "rasprover" / "src"


def _compile_and_run(tmp_path: Path, source: str) -> bytes:
    test_c = tmp_path / "test_ros_cdr.c"
    test_c.write_text(textwrap.dedent(source))
    exe = tmp_path / "test_ros_cdr"

    subprocess.run(
        [
            "gcc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(RASPROVER_SRC),
            str(test_c),
            str(RASPROVER_SRC / "app_ros_cdr.c"),
            "-o",
            str(exe),
        ],
        check=True,
    )
    return subprocess.check_output([exe])


def test_joint_state_encoder_publishes_zero_stamp_before_time_sync(tmp_path: Path) -> None:
    payload = _compile_and_run(
        tmp_path,
        r"""
        #include "app_ros_cdr.h"

        #include <stdint.h>
        #include <stdio.h>

        int main(void)
        {
            uint8_t buf[160];
            const struct app_ros_joint_sample joints[] = {
                {
                    .name = "left_wheel_joint",
                    .position = 1.25,
                    .velocity = -0.5,
                },
                {
                    .name = "right_wheel_joint",
                    .position = -2.5,
                    .velocity = 0.75,
                },
            };
            const struct app_ros_time stamp = {0};
            size_t len = app_ros_encode_joint_state(buf, sizeof(buf), stamp, joints, 2);

            fwrite(buf, 1, len, stdout);
            return 0;
        }
        """,
    )

    assert len(payload) == 120
    assert payload[:4] == b"\x00\x01\x00\x00"
    assert struct.unpack_from("<II", payload, 4) == (0, 0)

    assert struct.unpack_from("<I", payload, 12) == (1,)
    assert payload[16:20] == b"\x00\x00\x00\x00"

    assert struct.unpack_from("<I", payload, 20) == (2,)
    assert struct.unpack_from("<I", payload, 24) == (17,)
    assert payload[28:45] == b"left_wheel_joint\x00"
    assert payload[45:48] == b"\x00\x00\x00"
    assert struct.unpack_from("<I", payload, 48) == (18,)
    assert payload[52:70] == b"right_wheel_joint\x00"
    assert payload[70:72] == b"\x00\x00"

    assert struct.unpack_from("<I", payload, 72) == (2,)
    assert struct.unpack_from("<dd", payload, 76) == (1.25, -2.5)
    assert struct.unpack_from("<I", payload, 92) == (2,)
    assert payload[96:100] == b"\x00\x00\x00\x00"
    assert struct.unpack_from("<dd", payload, 100) == (-0.5, 0.75)
    assert struct.unpack_from("<I", payload, 116) == (0,)
