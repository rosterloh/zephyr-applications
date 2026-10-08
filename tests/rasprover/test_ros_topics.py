from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
RASPROVER = REPO_ROOT / "applications" / "rasprover"


def test_joint_state_topic_defaults_to_global_ros_topic() -> None:
    kconfig = (RASPROVER / "Kconfig").read_text()
    app_picoros = (RASPROVER / "src" / "app_picoros.c").read_text()

    assert "config APP_PICOROS_JOINT_STATE_TOPIC" in kconfig
    assert 'default "joint_states"' in kconfig
    assert "#define JOINT_STATE_TOPIC       CONFIG_APP_PICOROS_JOINT_STATE_TOPIC" in app_picoros


def test_rasprover_overlay_removes_stale_zenoh_serial_alias() -> None:
    overlay = (REPO_ROOT / "applications" / "rasprover" / "boards" / "ros_driver_esp32_procpu.overlay").read_text()

    assert "zenoh-serial" not in overlay
    assert "ssd1306_ssd1306_128x32" in overlay
    assert "ina219@42" in overlay


def test_gimbal_topic_defaults_to_joint_state_command() -> None:
    kconfig = (RASPROVER / "Kconfig").read_text()
    app_picoros = (RASPROVER / "src" / "app_picoros.c").read_text()
    app_gimbal = (RASPROVER / "src" / "app_gimbal.c").read_text()

    assert "config APP_GIMBAL" in kconfig
    assert "config APP_PICOROS_GIMBAL_CMD_TOPIC" in kconfig
    assert 'default "rasprover/gimbal_cmd"' in kconfig
    assert "#define GIMBAL_CMD_TOPIC    CONFIG_APP_PICOROS_GIMBAL_CMD_TOPIC" in app_picoros
    assert "pan_joint" in app_gimbal
    assert "tilt_joint" in app_gimbal


def test_gimbal_command_subscriber_is_declared() -> None:
    app_picoros = (RASPROVER / "src" / "app_picoros.c").read_text()

    assert ".name = GIMBAL_CMD_TOPIC" in app_picoros
    assert ".user_callback = gimbal_cmd_handler" in app_picoros
    assert "picoros_subscriber_declare(&_node, &_sub_gimbal_cmd)" in app_picoros
    assert "app_gimbal_set_positions(pan, tilt)" in app_picoros
