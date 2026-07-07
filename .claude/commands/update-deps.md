---
description: Review, verify and merge the grouped Dependabot dependency PR
argument-hint: "[PR number] (optional; defaults to the open Dependabot python group PR) [dry-run]"
allowed-tools: Bash(gh:*), Bash(git:*), Bash(uv:*), Read, Edit
---

Verify and land the grouped Dependabot dependency update for this repo. Dependabot
is configured (`.github/dependabot.yml`) with the `uv` ecosystem + grouping, so a
routine update arrives as a single PR that already bumps `pyproject.toml` **and**
re-resolves `uv.lock`. Your job is to prove it's safe and merge it.

Arguments: `$ARGUMENTS`
- If a PR number is given, use that PR. Otherwise find the open Dependabot PR.
- If `dry-run` is present, do everything except the final merge.

Follow this repo's rules: every Python tool runs via `uv run` (never bare
`west`/`pytest`/`pip`, never activate `.venv`). Use `gh` for GitHub.

Steps:

1. **Find the PR.** `gh pr list --author 'app/dependabot' --state open --json number,title,headRefName`.
   Prefer the grouped `python` PR (branch like `dependabot/uv/...`). If there are
   several separate Dependabot PRs instead of one group (e.g. grouping not active
   yet), list them and ask whether to combine them the manual way before continuing.
   If none are open, say so and stop.

2. **Check it out.** `gh pr checkout <number>` (this creates/switches to the PR branch).

3. **Sync + verify.**
   - `uv sync`
   - `uv run pytest` (scoped to `tests/` via `testpaths`; expect the repo's own
     suite to pass).
   Capture the pass/fail counts — do not paraphrase from memory.

4. **Handle a bad member.** If `uv sync`/`uv lock` reports a resolution conflict, or
   a specific bump breaks the tests, identify the offending dependency and back it
   out: restore its previous constraint in `pyproject.toml`, `uv lock`, then re-run
   step 3. Record which dependency you excluded and why (one line). Known standing
   example: `pylink-square>=2.0` is unsatisfiable because `pyocd` pins
   `pylink-square<2.0` — it's already in the dependabot `ignore` list, so it should
   not appear; if it does, exclude it and flag that the ignore rule needs checking.

5. **Land it.**
   - If `dry-run`: report the verification result and the diff summary; stop without
     merging.
   - Otherwise, only if verification is green: merge with
     `gh pr merge <number> --squash --auto` so required CI checks still gate the
     merge (fall back to reporting if auto-merge is not enabled and checks are
     pending — do not force a merge past red/pending checks).
   - If verification failed and nothing could be safely backed out, do **not** merge;
     report what failed with the captured output.

6. **Report.** State: which PR, resolved-version changes (from `uv.lock`), test
   result with counts, anything excluded and why, and the merge outcome (merged /
   queued for auto-merge / left open).
