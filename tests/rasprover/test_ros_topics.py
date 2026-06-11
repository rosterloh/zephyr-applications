from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


def test_joint_state_topic_defaults_to_global_ros_topic() -> None:
    kconfig = (REPO_ROOT / "applications" / "rasprover" / "Kconfig").read_text()
    app_zenoh = (REPO_ROOT / "applications" / "rasprover" / "src" / "app_zenoh.c").read_text()

    assert "config APP_ZENOH_JOINT_STATE_KEY" in kconfig
    assert 'default "rt/joint_states"' in kconfig
    assert '#define JOINT_STATE_KEY          CONFIG_APP_ZENOH_JOINT_STATE_KEY' in app_zenoh


def test_rasprover_overlay_removes_stale_zenoh_serial_alias() -> None:
    overlay = (REPO_ROOT / "applications" / "rasprover" / "boards" / "ros_driver_esp32_procpu.overlay").read_text()

    assert "zenoh-serial" not in overlay
    assert "ssd1306_ssd1306_128x32" in overlay
    assert "ina219@42" in overlay


def test_gimbal_topic_defaults_to_joint_state_command() -> None:
    kconfig = (REPO_ROOT / "applications" / "rasprover" / "Kconfig").read_text()
    app_zenoh = (REPO_ROOT / "applications" / "rasprover" / "src" / "app_zenoh.c").read_text()

    assert "config APP_GIMBAL" in kconfig
    assert "config APP_ZENOH_GIMBAL_CMD_KEY" in kconfig
    assert 'default "rt/rasprover/gimbal_cmd"' in kconfig
    assert '#define GIMBAL_CMD_KEY          CONFIG_APP_ZENOH_GIMBAL_CMD_KEY' in app_zenoh
    assert "pan_joint" in (REPO_ROOT / "applications" / "rasprover" / "src" / "app_gimbal.c").read_text()
    assert "tilt_joint" in (REPO_ROOT / "applications" / "rasprover" / "src" / "app_gimbal.c").read_text()
