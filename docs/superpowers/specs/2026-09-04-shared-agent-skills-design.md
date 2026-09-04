# Shared skills for Claude Code and Codex

Date: 2026-09-04

## Problem

The eight skills in this repo live in `.claude/skills/` and are visible only to
Claude Code. Codex, used against the same workspace, sees none of them.

## What the two tools actually require

They are nearly the same system under different directory names:

| | Claude Code | Codex |
|---|---|---|
| Repo-local path | `.claude/skills/` | `.agents/skills/` |
| Entry file | `SKILL.md` | `SKILL.md` |
| Frontmatter | `name`, `description` | `name`, `description` |
| Sub-directories | `references/`, `scripts/` | `references/`, `scripts/`, `assets/` |
| Loading | progressive disclosure | progressive disclosure |

Codex's documented search order is `$CWD/.agents/skills`,
`$CWD/../.agents/skills`, **`$REPO_ROOT/.agents/skills`**, `$HOME/.agents/skills`,
`/etc/codex/skills`, then built-ins. So the only thing between these skills and
Codex is the directory name.

This mirrors the arrangement already in use at user level: canonical skills in
`~/.agents/skills/`, with `~/.claude/skills/<name>` symlinked into it.

## Design

**Canonical location is `.agents/skills/`, with `.claude/skills` a tracked
symlink to it.**

```
git mv .claude/skills .agents/skills
ln -s ../.agents/skills .claude/skills
```

`.agents/` is the tool-neutral name and the one Codex discovers natively, so it
is the canonical copy; the symlink is the compatibility shim, pointing the way
that needs no maintenance if Claude Code later reads `.agents/skills` directly
(at which point the symlink can simply be deleted).

Four call sites hard-code the old path and move with it:

- `scripts/check_skill_symbols.py` - the `SKILLS` root
- `mise.toml` - the `check-skills` task description
- `AGENTS.md` - two references in the workspace-updates section
- `.github/workflows/build.yml` - `paths-ignore` gains `.agents/**` in both the
  `push` and `pull_request` blocks, so a skills-only PR still skips six firmware
  builds

### The .gitignore workaround stays, and needs one edit

The global `~/.gitignore` excludes `/.claude/` wholesale, and this repo
un-ignores it to track the shared parts. That does **not** go away: the
`.claude/skills` symlink lives inside `.claude/` and has to be tracked, or a
fresh clone has no symlink and Claude Code sees no skills.

The rule also loses its trailing slash. `!/.claude/skills/` matches a
*directory*; the replacement is a symlink, which git does not treat as one, so
the un-ignore silently fails and `git add` refuses the link. `!/.claude/skills`
is what works. This was found by hitting it.

## Commands

`.claude/commands/update-deps.md` is converted to
`.agents/skills/update-deps/SKILL.md` and the `.claude/commands/` directory is
removed.

Codex has no repo-local slash-command mechanism to share with: its custom
prompts are user-level only (`~/.codex/prompts/`, top-level files only) and are
deprecated, with skills named as the replacement for reusable instructions.
Converting is therefore the only route to parity, and it costs nothing in Claude
Code, which invokes a skill by `/<skill-name>` just as it did the command.

Frontmatter maps as:

| Command field | Skill |
|---|---|
| `description` | kept as-is |
| `argument-hint` | folded into the body, which already documents the arguments |
| `allowed-tools` | **dropped - no equivalent** |

Accepted trade-off: the command is currently restricted to
`Bash(gh:*), Bash(git:*), Bash(uv:*), Read, Edit`. Neither tool's skill format
can express that, so as a skill it runs with full tool access. The procedure is
explicit about what it runs and both tools prompt for command approval, but this
is a real widening of blast radius for something whose last step merges a PR.

The stale `uv run` instructions in that file are corrected to `mise run` during
the conversion: the repo standardised on mise, and shipping a skill that tells an
agent to use the wrong invocation is a bug, not a cosmetic issue.

## Verification (all done)

- **Claude Code follows the whole-directory symlink.** Confirmed in-session:
  `update-deps` appeared in the available-skills list the moment its `SKILL.md`
  was written under `.agents/skills/`, reached through `.claude/skills`. The
  per-skill fallback below was not needed.
- `mise run check-skills` passes against the new root.
- `git ls-files -s .claude/skills` reports mode `120000` and the blob resolves
  to `../.agents/skills`.
- `git ls-files .agents/skills` counts 52: the 51 files moved plus the new
  `update-deps/SKILL.md`. Every move is recorded as a rename, so history is
  preserved.

`check-skills` enforces one thing worth knowing before adding any skill here:
every skill needs a `## Validation Checklist` section. The converted
`update-deps` failed on that until one was written.

**Fallback, unused.** If a future Claude Code stops following the directory
symlink, replace the single link with per-skill links - same canonical
location, no other change.

## Not in scope

- `update-deps` depends on `gh`, which is not currently installed on this
  machine. Unrelated to the move.
- Sharing `~/.claude/skills` and `~/.agents/skills` at user level, which is
  already done.
