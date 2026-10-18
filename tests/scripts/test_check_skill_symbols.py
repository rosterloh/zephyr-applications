import importlib.util
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "check_skill_symbols.py"
spec = importlib.util.spec_from_file_location("check_skill_symbols", SCRIPT)
css = importlib.util.module_from_spec(spec)
spec.loader.exec_module(css)


def _skill(root: Path, name: str, body: str = "## Validation Checklist\n") -> Path:
    d = root / name
    (d / "references").mkdir(parents=True)
    (d / "SKILL.md").write_text(body)
    return d


def test_repo_skills_plus_shared_zephyr_skills_only(tmp_path):
    repo, user, store = tmp_path / "repo", tmp_path / "user", tmp_path / "dotfiles"
    _skill(repo, "mise")
    _skill(repo, "zephyr-workspace")
    shared = _skill(store, "zephyr-boards")
    _skill(user, "dream")
    (user / "zephyr-boards").symlink_to(shared)

    skills = css.discover_skills(repo, user)

    assert sorted(skills) == ["mise", "zephyr-boards", "zephyr-workspace"]
    assert css.shared_skill_names(skills, user) == ["zephyr-boards"]


def test_skill_in_both_roots_is_reported(tmp_path):
    repo, user = tmp_path / "repo", tmp_path / "user"
    _skill(repo, "zephyr-kernel")
    _skill(user, "zephyr-kernel")

    assert css.duplicate_skills(repo, user) == ["zephyr-kernel"]


def test_cross_root_reference_resolves(tmp_path):
    repo, user = tmp_path / "repo", tmp_path / "user"
    _skill(repo, "zephyr-workspace", "## Validation Checklist\nsee zephyr-kernel/references/netbuf.md\n")
    kernel = _skill(user, "zephyr-kernel", "## Validation Checklist\n`references/netbuf.md`\n")
    (kernel / "references" / "netbuf.md").write_text("x")

    assert css.check_references(css.discover_skills(repo, user)) == []
