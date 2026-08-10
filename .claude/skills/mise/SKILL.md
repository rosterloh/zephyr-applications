---
name: mise
description: >
  Authoring and running tasks in this workspace's mise.toml — the tool-version
  pin, the auto-activated uv venv, and the `[tasks.*]` task runner that wraps
  every west/python invocation. Use when adding or editing a task, adding an
  app or board to the `app` task's allowed list, pinning a tool in `[tools]`,
  debugging "task not found" / empty `usage_*` variables / a task that runs
  with the wrong interpreter, or when a command needs a sub-shell or
  background invocation. Triggers on mise.toml, `[tasks.`, `[tools]`,
  `mise run`, `mise x --`, `usage_*`, MISE_PROJECT_ROOT, "add a task",
  "add an app to the build task", "why isn't the venv active".
---

# mise (task runner + tool versions)

Targets mise 2026.8.3. Full docs: https://mise.jdx.dev/tasks/

`mise.toml` is the single entry point for this workspace: it pins the toolchain
(`[tools]`), auto-activates `.venv/` for anything it runs
(`_.python.venv`), and defines every project task (`[tasks.*]`). CLAUDE.md
covers *using* the tasks — this skill covers *changing* them.

## Before writing anything, look at what exists

```bash
mise tasks                  # every task + description
mise tasks info <task>      # resolved run script, source file, args
```

`mise tasks info` prints the script after TOML parsing, which is the fastest
way to confirm a quoting or interpolation fix actually landed.

## Task anatomy

```toml
[tasks."west-update"]                 # quote names containing '-'
run = "west update --narrow"
description = "..."                   # shows in `mise tasks`; keep it accurate
```

- **`run`** — one command, or a multi-line string for a script. Runs under `sh`,
  not zsh/bash: no arrays, no `[[ ]]`, no `pipefail`.
- **`description`** — the only documentation an agent sees in `mise tasks`.
- **`depends`** — prerequisite tasks. This repo instead chains explicitly in
  `run` (see `setup`), which keeps the ordering visible in one place.
- **`sources`/`outputs`** — skip the task when sources are unchanged. Not used
  here; `west build` does its own up-to-date checking.

### Arguments: the `usage` spec

```toml
[tasks.flash]
usage = '''
arg "<app>" help="App name (e.g. motor_controller)"
flag "-b --board <board>" help="Override target board"
flag "--sysbuild" help="Build with sysbuild"
'''
run = "west flash --build-dir builds/${usage_app}"
```

Each declared name arrives in the environment as `usage_<name>`:
`${usage_app}`, `${usage_board}`, `${usage_sysbuild}`. A valueless flag is the
string `"true"` when passed and **empty when absent** — always compare against
`"true"` rather than testing for non-empty:

```sh
[ "${usage_sysbuild}" = "true" ] && EXTRA="--sysbuild"
```

## Traps

- **Multi-line `run` needs `set -e`.** Without it a failing line is ignored and
  the task exits 0 — a build failure reported as success. Every multi-line task
  in this file starts with `set -e`.
- **`"""` interpolates, `'''` does not.** In a `"""` block a literal backslash
  sequence must be doubled (`printf '%s\\n'` in the `app` task) — TOML consumes
  one level. Any script containing heredocs, `sed` expressions, or `\n` belongs
  in a `'''` block (see `patch-zenoh`).
- **`$MISE_PROJECT_ROOT`, not `$PWD`.** Tasks run from wherever the user invoked
  them. Anything touching `deps/` or `builds/` must anchor on
  `${MISE_PROJECT_ROOT}` (see `sdk-install`, `patch-zenoh`).
- **Nested invocations need the prefix.** A task calling another task uses
  `mise run <task>` (see `agent-build` → `app`); a sub-shell or backgrounded
  command outside a task needs `mise x -- <cmd>`, or it gets the system Python
  and no `.venv`.
- **A new app or board must be added to the `app` task's `case`.** The
  allowlist is deliberate: an unlisted board exits 2 rather than silently
  building something untested. `agent-build` forwards to `app`, so one edit
  covers both.
- **`[tools]` changes need `mise install`.** Editing a version pin does not
  fetch it; `mise run` will use the old one until installed.

## Validation Checklist

- [ ] `mise tasks` lists the new/renamed task with its description.
- [ ] `mise tasks info <task>` shows the script with interpolation resolved as
      intended (no doubled `$`, no swallowed backslashes).
- [ ] The task fails loudly on a bad input — `mise run <task> nonsense` exits
      non-zero rather than proceeding.
- [ ] A multi-line task propagates failure: it exits non-zero when an inner
      command fails (`set -e` present).
- [ ] For a build/flash task: the artifact lands in `builds/<app>/`, not a
      top-level `build/`.
