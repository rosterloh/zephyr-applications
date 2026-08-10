# Working in this repo

Zephyr workspace driven by `mise` (tool versions + task runner) + `uv` (Python dependency lock/venv) + `west` (Zephyr meta-tool). Read this before running anything.

## Python environment: always `mise run` / `mise x --`

`mise.toml` pins the Python and `uv` versions and activates `.venv/` automatically for any `mise run <task>` or `mise x --` invocation. You never activate the venv yourself.

```bash
mise run <task> [args]      # tasks (preferred) — see below
mise x -- west <args>       # ad-hoc west calls outside a task
mise x -- python <script>   # ad-hoc python
```

Forbidden:

- `source .venv/bin/activate` — don't activate, don't suggest activating.
- Bare `west`, `python`, `pip`, `pytest` outside `mise run`/`mise x --`. They will hit the wrong interpreter or fail entirely.
- `pip install …` — dependencies are pinned in `pyproject.toml`/`uv.lock`; use `mise run sync` (`uv sync`) if something is missing and check in the lockfile.

If a command needs a sub-shell or background invocation, prefix the inner command with `mise x --` too (e.g. `nohup mise x -- west build … &`).

## Build apps via `mise run app`, not bare `west build`

Per-app build dirs live at `builds/<app>/`. The `app` task takes a bare app name, looks up the per-app default board (and an allowed-board list), and builds. Pass `--board` to override; pass `--sysbuild` for MCUboot integration.

```bash
mise run app motor_controller                       # default: robotis_openrb_150
mise run app joystick_controller                    # default: adafruit_qt_py_esp32s3/esp32s3/procpu
mise run app embedded_vision                        # default: arduino_nicla_vision
mise run app force_sensor                           # default: adafruit_qt_py_esp32c3
mise run app pico_fw                                # default: rpi_pico/rp2040/w
mise run app rasprover --sysbuild                   # rasprover hw build (ros_driver/esp32 + MCUboot)
mise run app rasprover --board native_sim/native/64 # rasprover native_sim
mise run flash motor_controller                     # flash a previously built app
```

A board outside the app's allowed list is rejected — update the `case` in `mise.toml`'s `app` task to add new boards.

For agent-driven builds where you want truncated logs (and a full log on disk), use `agent-build`. It accepts the same args as `app` and writes the full log to `logs/<app>-build.log`:

```bash
mise run agent-build motor_controller
# → builds with -p always, writes logs/motor_controller-build.log,
#   prints last 5 lines on success, last 50 on failure.
# Override tail counts with TAIL_S / TAIL_F.
```

If you must call `west build` directly, always pass `--build-dir builds/<app>` so artifacts don't pollute the repo root, and use the bare app name (e.g. `builds/motor_controller`, not `builds/applications/motor_controller`).


## Workspace updates

```bash
mise run setup            # first-time: west update + SDK install + blobs + zenoh patch
mise run west-update      # refresh deps/ after pulling new manifest revisions
mise run sdk-install      # reinstall SDK toolchains (version pinned in deps/zephyr/SDK_VERSION)
mise run check-skills     # after west-update: flag Zephyr API drift in .claude/skills/
```

**Run `check-skills` after every `west-update`.** It validates every `CONFIG_*`
symbol cited in `.claude/skills/` against the Kconfig tree in `deps/`, and warns
when a skill's `Validated against:` stamp no longer matches the Zephyr checkout.
It also checks each skill's structure: every `references/*.md` pointer resolves,
every reference file is reachable from its `SKILL.md`, and every skill carries a
`## Validation Checklist`.
Assigning a renamed or removed symbol in a handwritten fragment (`prj.conf`,
`boards/*.conf`, `--extra-conf`) aborts the Kconfig stage outright —
`scripts/kconfig/kconfig.py` sets `warn_assign_undef` for handwritten input and
then turns the warning into `error: Aborting due to Kconfig warnings`. A stale
skill therefore sends you into a build failure whose real cause is a doc, not
your code.
It is deliberately *not* part of `west-update` — doc drift should not fail a
dependency update. Deeper API drift (function signatures, removed C APIs) is not
covered and still needs a manual pass over `doc/releases/migration-guide-*.rst`.

The west.yml uses `name-allowlist` to clone only the modules these apps need; do not remove modules from that list to "fix" missing-symbol errors without checking what depends on them.

## Layout

- `applications/<app>/` — Zephyr apps (`prj.conf`, `CMakeLists.txt`, `src/`, optional `boards/<board>.overlay`).
- `boards/<vendor>/<board>/` — out-of-tree board definitions.
- `deps/zephyr/` — Zephyr tree (managed by west, gitignored).
- `deps/modules/lib/rosterloh-drivers/` — out-of-tree drivers repo. Tracks `main` via west.yml. Local edits during PR development are fine; commit them in that repo, not this one.
- `deps/modules/lib/zenoh/` — zenoh-pico, patched by `mise run patch-zenoh`.
- `builds/<app>/` — build outputs (gitignored).
- `logs/` — `agent-build` log destination.

## Formatting

- Python: `mise run fmt` (ruff format, line-length 120).
- C / Zephyr code: clang-format using the in-tree `.clang-format`. Verify with `mise x -- clang-format --dry-run --Werror <files>`.

## Out-of-tree modules and PR work

When iterating on `deps/modules/lib/rosterloh-drivers` (or zenoh) you are editing a real git checkout. Standard flow:

1. Branch and commit inside the module dir.
2. Push and open a PR against that module's repo (e.g. `rosterloh/zephyr-drivers`).
3. Verify dependent apps still build from this workspace: `mise run app motor_controller`, etc.
4. After merge: `mise run west-update` to advance the pinned `main` ref locally.

Do **not** vendor module changes into this repo; west owns those paths.

## Don'ts

- Don't `cd deps/zephyr` to run west commands — run them from the workspace root.
- Don't create a top-level `build/` directory; always pass `--build-dir builds/<app>`.
- Don't commit `.venv/`, `builds/`, `deps/`, or `logs/` (already gitignored).
- Don't add a `requirements.txt` or `Pipfile`; deps go in `pyproject.toml` and are locked by `uv`.
