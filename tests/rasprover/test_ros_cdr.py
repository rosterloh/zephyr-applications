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


def _joint_state_payload(names: list[str], positions: list[float]) -> bytes:
    payload = bytearray(b"\x00\x01\x00\x00")
    payload += struct.pack("<II", 0, 0)
    payload += struct.pack("<I", 1)
    payload += b"\x00\x00\x00\x00"
    payload += struct.pack("<I", len(names))
    for name in names:
        encoded = name.encode() + b"\x00"
        payload += struct.pack("<I", len(encoded))
        payload += encoded
        while (len(payload) - 4) % 4:
            payload += b"\x00"
    payload += struct.pack("<I", len(positions))
    while (len(payload) - 4) % 8:
        payload += b"\x00"
    payload += struct.pack("<" + "d" * len(positions), *positions)
    payload += struct.pack("<I", 0)
    payload += struct.pack("<I", 0)
    return bytes(payload)


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


def test_joint_state_decoder_extracts_named_gimbal_positions(tmp_path: Path) -> None:
    payload = _joint_state_payload(["tilt_joint", "pan_joint"], [0.25, -0.5])
    source = f"""
    #include "app_ros_cdr.h"
    #include <stdint.h>
    #include <stdio.h>

    static const uint8_t payload[] = {{{", ".join(str(b) for b in payload)}}};

    int main(void)
    {{
        struct app_ros_joint_command cmd;
        if (!app_ros_decode_joint_command(payload, sizeof(payload), &cmd)) {{
            return 1;
        }}
        printf("%d %.3f %.3f\\n", cmd.has_pan && cmd.has_tilt, cmd.pan_position, cmd.tilt_position);
        return 0;
    }}
    """

    output = _compile_and_run(tmp_path, source)
    assert output == b"1 -0.500 0.250\n"


def test_joint_state_decoder_rejects_missing_tilt(tmp_path: Path) -> None:
    payload = _joint_state_payload(["pan_joint"], [0.0])
    source = f"""
    #include "app_ros_cdr.h"
    #include <stdint.h>

    static const uint8_t payload[] = {{{", ".join(str(b) for b in payload)}}};

    int main(void)
    {{
        struct app_ros_joint_command cmd;
        return app_ros_decode_joint_command(payload, sizeof(payload), &cmd) ? 1 : 0;
    }}
    """

    _compile_and_run(tmp_path, source)
