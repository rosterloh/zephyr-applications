import json
import subprocess
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "ci" / "footprint.py"

SYSBUILD_LOG = """\
[9/16] Performing build step for 'rasprover'
Memory region         Used Size  Region Size  %age Used
           FLASH:      1000 B    2000 B     50.00%
     dram0_0_seg:      1900 B      2 KB     92.77%
        IDT_LIST:           0 B         8 KB      0.00%
Generating files
[11/16] Performing build step for 'mcuboot'
Memory region         Used Size  Region Size  %age Used
           FLASH:         1 KB         1 KB    100.00%
     mcuboot_hdr:          32 B         32 B    100.00%
"""


def report(tmp_path: Path, log: str, db: Path) -> subprocess.CompletedProcess[str]:
    path = tmp_path / "build.log"
    path.write_text(log)
    return subprocess.run(
        [sys.executable, SCRIPT, path, "--app", "demo", "--db", db], capture_output=True, text=True, check=False
    )


def test_splits_images_and_drops_empty_regions(tmp_path: Path) -> None:
    db = tmp_path / "db.json"
    out = report(tmp_path, SYSBUILD_LOG, db)
    assert out.returncode == 0, out.stderr
    assert json.loads(db.read_text())["rasprover"] == {"FLASH": [1000, 2000], "dram0_0_seg": [1900, 2048]}
    assert "IDT_LIST" not in out.stdout
    assert "No baseline" in out.stdout


def test_table_ends_at_first_non_row(tmp_path: Path) -> None:
    log = "Memory region  Used Size  Region Size  %age Used\n  A:  1 B  2 B  50.00%\nnoise\n  B:  1 B  2 B  50.00%\n"
    db = tmp_path / "db.json"
    assert report(tmp_path, log, db).returncode == 0
    assert list(json.loads(db.read_text())["app"]) == ["A"]


def test_reports_change_and_flags_nearly_full(tmp_path: Path) -> None:
    db = tmp_path / "db.json"
    db.write_text(json.dumps({"rasprover": {"FLASH": [900, 2000], "dram0_0_seg": [1900, 2048]}}))
    out = report(tmp_path, SYSBUILD_LOG, db).stdout
    assert "| rasprover | FLASH | 1,000 | +100 | 50.0% |" in out
    assert "| rasprover | dram0_0_seg | 1,900 | 0 | 92.8% |" in out
    assert "| mcuboot | FLASH | 1,024 | new | 100.0% ⚠️ |" in out
    assert "| mcuboot | mcuboot_hdr | 32 | new | 100.0% |" in out


def test_log_without_a_table_fails_and_keeps_the_baseline(tmp_path: Path) -> None:
    db = tmp_path / "db.json"
    db.write_text("{}")
    out = report(tmp_path, "nothing here\n", db)
    assert out.returncode != 0
    assert db.read_text() == "{}"
