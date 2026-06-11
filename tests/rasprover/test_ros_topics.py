from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


def test_joint_state_topic_defaults_to_global_ros_topic() -> None:
    kconfig = (REPO_ROOT / "applications" / "rasprover" / "Kconfig").read_text()
    app_zenoh = (REPO_ROOT / "applications" / "rasprover" / "src" / "app_zenoh.c").read_text()

    assert "config APP_ZENOH_JOINT_STATE_KEY" in kconfig
    assert 'default "rt/joint_states"' in kconfig
    assert '#define JOINT_STATE_KEY          CONFIG_APP_ZENOH_JOINT_STATE_KEY' in app_zenoh
