# Working in this repo

Zephyr workspace driven by `uv` (Python env) + `poethepoet` (task runner) + `west` (Zephyr meta-tool). Read this before running anything.

## Python environment: always `uv run`

Every Python tool — including `west` — must be invoked through `uv run`. The repo's venv lives at `.venv/` but you never activate it.

```bash
uv run west <args>          # west calls
uv run poe <task> [args]    # poe tasks (preferred)
uv run python <script>      # ad-hoc python
```

Forbidden:

- `source .venv/bin/activate` — don't activate, don't suggest activating.
- Bare `west`, `python`, `pip`, `pytest`. They will hit the wrong interpreter or fail entirely.
- `pip install …` — dependencies are pinned in `pyproject.toml`; use `uv sync` if something is missing and check it in.

If a command needs a sub-shell or background invocation, prefix the inner command with `uv run` too (e.g. `nohup uv run west build … &`).

## Build apps via `poe`, not bare `west build`

Per-app build dirs live at `builds/<app>/`. The `poe` tasks already encode the right board, build dir, and flags. Use them.

```bash
uv run poe build-motor          # motor_controller
uv run poe build-vision         # embedded_vision
uv run poe build-force          # force_sensor
uv run poe build-joystick       # joystick_controller
uv run poe build-rasprover      # rasprover (sysbuild + MCUboot)
uv run poe sim-rasprover        # rasprover native_sim
uv run poe build <app-path>     # generic; needs MY_BOARD in .env or env
uv run poe flash <app-path>     # flash a previously built app
```

For agent-driven builds where you want truncated logs (and a full log on disk), use `agent-build`:

```bash
uv run poe agent-build applications/motor_controller
# → builds with -p always, writes logs/motor_controller-build.log,
#   prints last 5 lines on success, last 50 on failure.
# Override with TAIL_S / TAIL_F.
```

If you must call `west build` directly, always pass `--build-dir builds/<app>` so artifacts don't pollute the repo root.

`pico_fw` is the exception: it has its own west workspace under `applications/pico_fw/` due to a cyw43 module conflict. Build it from there.

## Workspace updates

```bash
uv run poe setup            # first-time: west update + SDK install + blobs + zenoh patch
uv run poe west-update      # refresh deps/ after pulling new manifest revisions
uv run poe sdk-install      # reinstall SDK toolchains (version pinned in deps/zephyr/SDK_VERSION)
```

The west.yml uses `name-allowlist` to clone only the modules these apps need; do not remove modules from that list to "fix" missing-symbol errors without checking what depends on them.

## Layout

- `applications/<app>/` — Zephyr apps (`prj.conf`, `CMakeLists.txt`, `src/`, optional `boards/<board>.overlay`).
- `boards/<vendor>/<board>/` — out-of-tree board definitions.
- `deps/zephyr/` — Zephyr tree (managed by west, gitignored).
- `deps/modules/lib/rosterloh-drivers/` — out-of-tree drivers repo. Tracks `main` via west.yml. Local edits during PR development are fine; commit them in that repo, not this one.
- `deps/modules/lib/zenoh/` — zenoh-pico, patched by `poe patch-zenoh`.
- `builds/<app>/` — build outputs (gitignored).
- `logs/` — `agent-build` log destination.
- `.env` — selects `MY_BOARD` for the generic `build` task.

## Formatting

- Python: `uv run poe fmt` (ruff format, line-length 120).
- C / Zephyr code: clang-format using the in-tree `.clang-format`. Verify with `uv run clang-format --dry-run --Werror <files>`.

## Out-of-tree modules and PR work

When iterating on `deps/modules/lib/rosterloh-drivers` (or zenoh) you are editing a real git checkout. Standard flow:

1. Branch and commit inside the module dir.
2. Push and open a PR against that module's repo (e.g. `rosterloh/zephyr-drivers`).
3. Verify dependent apps still build from this workspace: `uv run poe build-motor`, etc.
4. After merge: `uv run poe west-update` to advance the pinned `main` ref locally.

Do **not** vendor module changes into this repo; west owns those paths.

## Don'ts

- Don't `cd deps/zephyr` to run west commands — run them from the workspace root.
- Don't create a top-level `build/` directory; always pass `--build-dir builds/<app>`.
- Don't commit `.venv/`, `builds/`, `deps/`, or `logs/` (already gitignored).
- Don't add a `requirements.txt` or `Pipfile`; deps go in `pyproject.toml` and are locked by `uv`.
