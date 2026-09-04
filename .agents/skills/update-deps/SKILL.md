---
name: update-deps
description: >
  Review, verify and merge the grouped Dependabot dependency PR for this
  workspace. Use when asked to land, check or merge the Dependabot / dependency
  update PR, refresh pinned Python dependencies, or deal with uv.lock bumps.
  Takes an optional PR number (defaults to the open Dependabot python group PR)
  and an optional `dry-run` to stop short of merging.
---

Verify and land the grouped Dependabot dependency update for this repo. Dependabot
is configured (`.github/dependabot.yml`) with the `uv` ecosystem + grouping, so a
routine update arrives as a single PR that already bumps `pyproject.toml` **and**
re-resolves `uv.lock`. Your job is to prove it's safe and merge it.

Arguments:
- If a PR number is given, use that PR. Otherwise find the open Dependabot PR.
- If `dry-run` is present, do everything except the final merge.

Follow this repo's rules: every Python tool runs through mise, which activates
the pinned venv — `mise run <task>` for tasks, `mise x -- <cmd>` for ad-hoc
invocations. Never run bare `west`/`pytest`/`pip`, and never activate `.venv`.
Use `gh` for GitHub.

Steps:

1. **Find the PR.** `gh pr list --author 'app/dependabot' --state open --json number,title,headRefName`.
   Prefer the grouped `python` PR (branch like `dependabot/uv/...`). If there are
   several separate Dependabot PRs instead of one group (e.g. grouping not active
   yet), list them and ask whether to combine them the manual way before continuing.
   If none are open, say so and stop.

2. **Check it out.** `gh pr checkout <number>` (this creates/switches to the PR branch).

3. **Sync + verify.**
   - `mise run sync`
   - `mise x -- pytest` (scoped to `tests/` via `testpaths`). Some tests require a
     toolchain or Zephyr headers not present outside a west build and may fail for
     reasons unrelated to the bump — do not assume a fully green suite.
   Capture the pass/fail counts — do not paraphrase from memory.

3b. **Judge on regression, not absolute pass.** A raw pass/fail count is only
   meaningful against a baseline. If any test fails, re-run that same test on `main`
   (`git stash -u` or `mise x -- uv run --frozen pytest <test>` after `git checkout main`) and
   compare. Only failures the bump *introduced* block the merge; pre-existing or
   environmental failures are noted and ignored. The baseline run mutates the env
   (a frozen run on `main` can downgrade packages), so `git checkout` back to the PR
   branch and re-run `mise run sync` before merging.

4. **Handle a bad member.** If `mise run sync` / `mise x -- uv lock` reports a resolution conflict, or
   a specific bump breaks the tests, identify the offending dependency and back it
   out: restore its previous constraint in `pyproject.toml`, `mise x -- uv lock`, then re-run
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
     pending — do not force a merge past red/pending checks). With clean checks this
     often merges immediately rather than queuing. Confirm the actual outcome with
     `gh pr view <number> --json state,autoMergeRequest,mergeStateStatus` — report
     merged-vs-queued from that result, not from which command path you took.
   - If verification failed and nothing could be safely backed out, do **not** merge;
     report what failed with the captured output.

6. **Clean up.** If merged, return to `main`, `git pull --ff-only`, and delete the
   local PR branch.

7. **Report.** State: which PR, resolved-version changes (from `uv.lock`), test
   result with counts (noting any pre-existing failures confirmed against the `main`
   baseline), anything excluded and why, and the merge outcome (merged / queued for
   auto-merge / left open).

## Validation Checklist

- [ ] The PR acted on is the grouped Dependabot one, confirmed from `gh pr list`
      output rather than assumed.
- [ ] `mise run sync` succeeded before any test run — a stale `.venv` makes the
      test result meaningless.
- [ ] Test pass/fail counts are quoted from the actual run, not paraphrased.
- [ ] Any failure was compared against a `main` baseline, so only regressions
      the bump introduced were treated as blocking.
- [ ] After a baseline run on `main`, the PR branch was restored and
      `mise run sync` re-run before merging.
- [ ] `dry-run` stopped before merging.
- [ ] The merge outcome was read back from
      `gh pr view --json state,autoMergeRequest,mergeStateStatus`, not inferred
      from which command was issued.
- [ ] Nothing was merged past red or pending required checks.
