import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "scripts" / "ci" / "affected-apps.sh"


def affected(*paths: str) -> list[str]:
    out = subprocess.run([SCRIPT], input="\n".join(paths) + "\n", capture_output=True, text=True, check=True)
    return out.stdout.split()


def every_app() -> list[str]:
    return sorted(p.name for p in (REPO_ROOT / "applications").iterdir() if p.is_dir() and p.name != "common")


def test_app_change_affects_only_that_app() -> None:
    assert affected("applications/protective_stop/src/api.c", "applications/protective_stop/tests.yaml") == [
        "protective_stop"
    ]


def test_common_module_affects_the_apps_that_include_it() -> None:
    assert affected("applications/common/picoros/Kconfig") == ["joystick_controller", "rasprover"]


def test_docs_and_host_tests_affect_nothing() -> None:
    assert affected("README.md", "docs/x.md", ".claude/settings.json", "tests/rasprover/test_ros_topics.py") == []


def test_shared_inputs_affect_every_app() -> None:
    assert affected("applications/pico_fw/src/main.c", "west.yml") == every_app()
    assert affected("boards/eit/ros_driver/board.yml") == every_app()
