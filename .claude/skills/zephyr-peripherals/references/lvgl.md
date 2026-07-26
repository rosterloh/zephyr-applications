# LVGL

## Overview

Expert guidance for building LVGL v9 graphical UIs on Zephyr — what to
configure, how to structure screens, how to keep the threading model
sane, and how to iterate fast on the host with `native_sim` before
spending a flash cycle on real hardware.

### Table of Contents

1. [Decision tree: Is LVGL the right thing?](#decision-tree)
2. [Kconfig essentials](#kconfig-essentials)
3. [LVGL threading model — the #1 gotcha](#threading)
4. [Screen lifecycle and the build/update split](#screens)
5. [Widget cookbook (label, chart, flex)](#widgets)
6. [v8 → v9 API drift](#api-drift)
7. [Memory and heap budgeting](#memory)
8. [Input — touch and physical buttons](#input)
9. [native_sim for rapid iteration](#native-sim)
10. [Common bugs and gotchas](#bugs)
11. [Troubleshooting](#troubleshooting)

---

### <a name="decision-tree"></a>Decision tree

```
Need a graphical UI on Zephyr?
├── Static text on a small OLED? -> probably zephyr-display + cfb is enough
├── Touchable interface, multiple "screens", styled widgets? -> LVGL
└── Highly custom rendering, no widgets? -> raw display API + framebuffer
```

LVGL gives you: widgets (label, button, chart, flex/grid containers),
styling (themes, colors, fonts), input handling (touch, encoder,
keypad), and a small flush-to-display driver shim. In return it costs
~100–200 KB code + a configurable heap pool (default 32 KiB; bump
heavily once you add charts, fonts beyond Montserrat 14, or many
screens).

---

### <a name="kconfig-essentials"></a>Kconfig essentials

Minimum to bring LVGL up:

```kconfig
CONFIG_DISPLAY=y                 # Zephyr display subsystem
CONFIG_LVGL=y                    # LVGL itself
CONFIG_LV_Z_MEM_POOL_SIZE=131072 # default 32 KiB is too small in practice
```

Per-widget enables — none on by default in v9, you only get what you
ask for:

```kconfig
CONFIG_LV_USE_LABEL=y
CONFIG_LV_USE_BUTTON=y
CONFIG_LV_USE_FLEX=y
CONFIG_LV_USE_GRID=y
CONFIG_LV_USE_CHART=y            # required for lv_chart_*
CONFIG_LV_USE_THEME_DEFAULT=n    # the dark default theme is opinionated
                                 # — turn off if you're styling yourself
```

Fonts (each adds 2–10 KB; only enable what you actually use):

```kconfig
CONFIG_LV_FONT_MONTSERRAT_12=y
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_MONTSERRAT_28=y
```

Input subsystem (touch, encoder, keypad routed via Zephyr Input → LVGL
indev):

```kconfig
CONFIG_INPUT=y
CONFIG_INPUT_MODE_THREAD=y       # poll input from a thread, not LVGL ticks
```

**Pool sizing.** The `CONFIG_LV_Z_MEM_POOL_SIZE` default (32 KiB) is
fine for "two screens with a few labels". Realistic numbers:

| Workload | Recommended pool |
|---|---|
| One screen, few labels | 32 KiB |
| Two-three screens, ~70 widgets, Montserrat 28 glyphs | 128 KiB |
| Charts: 3 charts × 2 series × 120 points + chart bookkeeping | +32 KiB |

Comment the **why** next to the symbol so the next contributor doesn't
trim it back:

```conf
# Two screens with ~70 combined widgets plus Montserrat 28 glyph
# allocations push LVGL's heap well past the 32 KB default.
# Bumped from 131072 to 163840 to fund the Battery screen's three
# lv_chart widgets (two 120-point series each) and bookkeeping.
CONFIG_LV_Z_MEM_POOL_SIZE=163840
```

---

### <a name="threading"></a>LVGL threading model — the #1 gotcha

**LVGL is not thread-safe.** Every call into `lv_*` (object creation,
widget mutation, `lv_timer_handler`, `lv_scr_load`) must run on the
same thread. In Zephyr applications this is normally a dedicated UI
thread that owns the LVGL state.

The standard pattern:

```c
static void ui_thread(void)
{
    const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

    build_ui();                       /* construct screens once */
    if (device_is_ready(display)) {
        display_blanking_off(display);
    }

    while (true) {
        /* Drain inbound events (zbus, queues, msgqs) — but only call
         * lv_* on data you've already copied into local storage. */
        while (zbus_sub_wait(&ui_sub, &chan, K_MSEC(50)) == 0) {
            dispatch(chan);          /* updates the model + lv_label_set_text */
            render_active();
        }
        lv_timer_handler();          /* drives animations, redraws, timers */
        k_sleep(K_MSEC(20));
    }
}

K_THREAD_DEFINE(ui_tid, UI_THREAD_STACK_SIZE, ui_thread,
                NULL, NULL, NULL, UI_THREAD_PRIORITY, 0, 0);
```

Stack budget: **at least 8 KiB**, ideally 16 KiB. LVGL build paths
recurse several frames deep, and font glyph rasterisation (Montserrat
28+) adds more. 4 KB is enough for a Hello-World but overflows the
moment you add a few screens or large fonts.

**The two cardinal rules:**
1. Build widgets and call all mutators from the LVGL thread only.
2. From other threads, push data through zbus/msgq/atomic globals; the
   LVGL thread reads it on its tick and updates widgets.

---

### <a name="screens"></a>Screen lifecycle and the build/update split

A "screen" in LVGL is just `lv_obj_create(NULL)` (no parent). To switch:
`lv_scr_load(screen_handle)`.

For multi-screen apps the established pattern in this workspace is one
module per screen with a four-function API:

```c
/* applications/<app>/src/ui/ui_screen_foo.h */
lv_obj_t *ui_screen_foo_build(void);                /* build once at boot */
void ui_screen_foo_update(const struct ui_model *m);/* refresh on tick/event */
void ui_screen_foo_tick(const struct ui_model *m);  /* OPTIONAL: ran every */
                                                    /* tick regardless of */
                                                    /* which screen is active */
const struct ui_statusbar *ui_screen_foo_get_statusbar(void);
```

`_build` returns the screen object; widget handles are stashed in
file-scope `static lv_obj_t *` pointers (the screen is a singleton, so
this is fine):

```c
static lv_obj_t *screen;
static lv_obj_t *value_label;

lv_obj_t *ui_screen_foo_build(void)
{
    screen = lv_obj_create(NULL);
    lv_obj_add_style(screen, &ui_style_screen, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);

    value_label = lv_label_create(screen);
    lv_label_set_text(value_label, "");
    return screen;
}

void ui_screen_foo_update(const struct ui_model *m)
{
    if (screen == NULL) return;     /* called before _build is harmless */
    lv_label_set_text_fmt(value_label, "%u", m->something);
}
```

**Why a `_tick` separate from `_update`?** Some screens (e.g. a graph)
need to consume samples *every* tick, even when they're not the
foreground screen, so the trail is already populated when the user
navigates to them. Call `_tick(model)` from the UI tick callback
unconditionally; only call `_update(model)` for the active screen via
your `render_active()` dispatch.

Boot-time order:

```c
home_scr    = ui_screen_home_build();
battery_scr = ui_screen_battery_build();
status_scr  = ui_screen_status_build();
/* …register input callbacks, push initial model state… */
lv_scr_load(home_scr);
tick_timer = lv_timer_create(tick_cb, 1000, NULL);
```

---

### <a name="widgets"></a>Widget cookbook

#### Label

```c
lv_obj_t *lbl = lv_label_create(parent);
lv_obj_add_style(lbl, &ui_style_label_muted, 0);
lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);   /* truncate, don't wrap */
lv_obj_set_width(lbl, LV_PCT(100));
lv_label_set_text(lbl, "");                        /* empty until first update */
```

#### Inline-recolored label (mini legend)

```c
lv_obj_t *legend = lv_label_create(c);
lv_label_set_recolor(legend, true);                /* enables #RRGGBB markers */
lv_label_set_text_fmt(legend, "#%06x 0# #%06x 1#",
                      (unsigned)lv_color_to_u32(UI_COLOR_ACCENT) & 0xFFFFFFu,
                      (unsigned)lv_color_to_u32(UI_COLOR_WARN)   & 0xFFFFFFu);
lv_obj_align(legend, LV_ALIGN_TOP_RIGHT, -4, 2);
lv_obj_set_style_text_font(legend, &lv_font_montserrat_12, 0);
```

Mask `lv_color_to_u32` with `0xFFFFFFu` — the alpha byte breaks the
six-hex-digit format string otherwise.

#### Flex column / row

```c
lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);          /* or _ROW */
lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_SPACE_BETWEEN,  /* main axis */
                              LV_FLEX_ALIGN_CENTER,         /* cross axis */
                              LV_FLEX_ALIGN_CENTER);
```

Children with `LV_PCT(48)` widths inside a row with `SPACE_BETWEEN`
gives a clean two-column layout with a gap. For the screen container
itself, set `LV_PCT(100), LV_PCT(100)` and a flex flow — children
stack top-to-bottom (column) with explicit pixel heights you can sum
to the panel height.

#### Chart (line, scrolling, multi-series)

```c
lv_obj_t *c = lv_chart_create(parent);
lv_obj_set_width(c, LV_PCT(100));
lv_obj_set_height(c, 47);
lv_obj_set_style_pad_all(c, 2, 0);
lv_obj_set_style_radius(c, 4, 0);
lv_obj_set_style_bg_color(c, UI_COLOR_PANEL, 0);
lv_obj_set_style_border_color(c, UI_COLOR_BORDER, 0);
lv_obj_set_style_border_width(c, 1, 0);

lv_chart_set_type(c, LV_CHART_TYPE_LINE);
lv_chart_set_update_mode(c, LV_CHART_UPDATE_MODE_SHIFT);    /* scroll left */
lv_chart_set_div_line_count(c, 0, 0);
lv_chart_set_point_count(c, CONFIG_FOO_GRAPH_WINDOW_S);     /* sample count */
lv_chart_set_axis_range(c, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);  /* v9 */

lv_chart_series_t *s0 = lv_chart_add_series(c, UI_COLOR_ACCENT,
                                            LV_CHART_AXIS_PRIMARY_Y);
lv_chart_series_t *s1 = lv_chart_add_series(c, UI_COLOR_WARN,
                                            LV_CHART_AXIS_PRIMARY_Y);
```

Pushing samples (`lv_chart_set_next_value` shifts the buffer in
`UPDATE_MODE_SHIFT`):

```c
int v = source_alive ? (int)reading : LV_CHART_POINT_NONE; /* gap on miss */
lv_chart_set_next_value(c, s0, v);
```

`LV_CHART_POINT_NONE` is `INT32_MAX` in v9. The chart stores points
internally; you do **not** need a separate ring buffer.

---

### <a name="api-drift"></a>v8 → v9 API drift

Always check `deps/modules/lib/gui/lvgl/lv_version.h` first:

```bash
$ cat deps/modules/lib/gui/lvgl/lv_version.h
#define LVGL_VERSION_MAJOR 9
#define LVGL_VERSION_MINOR 5
```

The most common drift bites:

| v8 | v9 | Notes |
|---|---|---|
| `lv_chart_set_range(c, axis, min, max)` | `lv_chart_set_axis_range(...)` | Same args, renamed function. Easy to miss because most other chart APIs are unchanged. |
| `lv_obj_clear_flag` | `lv_obj_remove_flag` | v9 has a v8 compat shim (`lv_api_map_v8.h`) so `clear_flag` still works — but new code should use `remove_flag`. |
| Default theme | `LV_USE_THEME_DEFAULT=n` recommended | v9 default is opinionated; turn off and roll your own if you have a brand. |
| `lv_color_hex(0xRRGGBB)` | unchanged | Still the canonical color constructor. |
| `lv_label_set_recolor` | unchanged | Format `#RRGGBB text#`. Mask u32 with 0xFFFFFF. |

`grep -r 'lv_chart_set_range\b' applications/` is a useful sanity
check after a Zephyr upgrade.

---

### <a name="memory"></a>Memory and heap budgeting

LVGL allocations come out of the Zephyr LV pool sized by
`CONFIG_LV_Z_MEM_POOL_SIZE`. Rough numbers:

- **Screen object tree**: ~1 KB per screen for the base obj + flex
  layout state.
- **Label**: ~80–200 B each, plus the rendered glyph cache (depends on
  font size and visible characters).
- **Font glyphs (Montserrat 28)**: ~6–10 KB once the cache fills.
- **Chart**: ~500 B widget + `series_count × point_count × 4 B` for
  the y-coord buffer. Three charts × 2 series × 120 points × 4 B ≈
  2.9 KB plus chart bookkeeping.
- **Status bar / event log grids**: linear in widget count.

If LVGL runs out of pool memory it does **not** abort by default — it
silently returns NULL from `lv_*_create`, which then segfaults a few
frames later when you try to use the handle. Symptoms: random hard
faults shortly after building a new screen, or a screen that looks
fine until you add one more widget. Bump the pool with margin
(20-30%) over your computed worst-case.

Spot-check after sizing changes: `west build` reports SRAM usage in
its tail; the LV pool is statically allocated so it shows up in that
line directly.

---

### <a name="input"></a>Input — touch and physical buttons

#### Touch via `lv_indev`

`CONFIG_INPUT=y` plus `CONFIG_INPUT_MODE_THREAD=y` plumbs touch events
from a Zephyr Input device (`gt911`, `ft5336`, `ili9xxx_touch`, etc.)
into LVGL's indev layer. LVGL then synthesizes `LV_EVENT_PRESSED`,
`LV_EVENT_CLICKED`, `LV_EVENT_RELEASED` on widgets under the touch
point.

Register a click callback per widget:

```c
lv_obj_add_flag(my_label, LV_OBJ_FLAG_CLICKABLE);
lv_obj_add_event_cb(my_label, on_click, LV_EVENT_CLICKED, NULL);
lv_obj_set_ext_click_area(my_label, 10);    /* widen the hit area */
```

Touch hardware is fragile in practice — controllers can fail to init,
INT lines can be miswired, hibernate states can survive soft resets.
**Always have a non-touch path to navigate the UI** during bring-up:

#### Button-driven navigation as a fallback

If touch is broken or you don't have it, drive the screen cycle from a
physical button (sw0). This pattern proved valuable in the
`beta_hri` work after touch failed on the panel:

```c
static void advance_screen(void)
{
    enum ui_screen_id next = next_in_cycle(model.active_screen);
    const struct ui_cmd_msg msg = { .screen = next, .refresh = true };
    (void)zbus_chan_pub(&ui_cmd_chan, &msg, K_NO_WAIT);
}

static void on_screen_tap(lv_event_t *e)  { (void)e; advance_screen(); }

/* In the button handler thread (NOT the LVGL thread): */
case BUTTON_ACTION_PRESS:
    advance_screen();   /* publishes; the UI thread consumes and lv_scr_load's */
    break;
```

The advance helper publishes a message; the UI thread consumes it and
calls `lv_scr_load`. This keeps all `lv_*` calls on the LVGL thread.

**Debounce GPIO buttons** — if your button service uses `GPIO_INT_EDGE_BOTH`
and submits a regular `k_work` per ISR, contact bounce can publish
multiple PRESS events for one physical press. Switch to a
`k_work_delayable` and `k_work_reschedule(&w, K_MSEC(30))` from the
ISR — the work fires only after the line has been quiet, killing
bounce. See the same trick in `applications/beta_hri/src/io/button_service.c`.

**One observer per channel.** When two threads both subscribe to a
button events channel, both will react to the same press. If one is a
stale leftover toggle (e.g. an old binary HOME↔STATUS handler living
on alongside a new 3-screen cycle), you get phantom advances. Audit
the `ZBUS_OBSERVERS(...)` list when adding new screen-switching logic.

---

### <a name="native-sim"></a>native_sim for rapid iteration

Building and flashing real hardware takes 10–30 s per cycle. For UI
work, run the same firmware on the host with `native_sim` — LVGL
renders into an SDL window and you iterate in seconds.

#### Board overlay

Create `boards/native_sim_native_64.overlay` for your app to match the
real panel resolution:

```dts
/* boards/native_sim_native_64.overlay
 * Match the STM32H745I-DISCO 480x272 panel resolution.
 */

&sdl_dc {
    height = <272>;
    width  = <480>;
};
```

(`sdl_dc` is the SDL display controller exposed by the
`native_sim` board's devicetree — it provides a host-window display
device that LVGL flushes into.)

#### Building

```bash
uv run west build -b native_sim/native/64 -p always \
                  -d builds/<app>_sim applications/<app>
./build/<app>_sim/zephyr/zephyr.exe
```

(or `_native_sim_native_64` build dir name if you have multiple
flavors; use `-p always` whenever you change configs to force a clean
configure.)

If your project uses `poe` tasks, add a sim variant alongside the
hardware build:

```toml
[tasks.build-sim]
cmd  = "west build -b native_sim/native/64 -p always -d builds/${proj}_sim applications/${proj}"
[tasks.build-sim.args]
proj = { default = "beta_hri" }
```

The `zephyr.exe` is a normal POSIX binary — run it directly, kill it
with Ctrl-C, attach `gdb` if you want.

#### Configuring the host build

Some Kconfigs that work on hardware will fail on `native_sim` — most
commonly anything board-specific (CAN, I2C, FDCAN, etc.). Strategies:

1. **Conditionally compile** in your app code:
   ```c
   #if DT_NODE_HAS_STATUS(DT_NODELABEL(fdcan1), okay)
   /* hardware-only */
   #else
   /* sim stub */
   #endif
   ```
   This pattern is used in `applications/beta_hri/src/io/bms_service.c`
   to stub the BMS path when CAN isn't available.

2. **Sim-only `prj.conf` overlays** (`boards/native_sim_native_64.conf`):
   ```conf
   CONFIG_CAN=n
   CONFIG_NETWORKING=n
   ```

3. **Synthetic input.** When a real button isn't present, fall back to
   a periodic timer that publishes synthetic button events:
   ```c
   #if !DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
   static void demo_button_timer(struct k_timer *t) {
       const struct button_event_msg m = { .id = BUTTON_ID_0,
                                            .action = BUTTON_ACTION_PRESS };
       (void)zbus_chan_pub(&button_events_chan, &m, K_NO_WAIT);
   }
   K_TIMER_DEFINE(t, demo_button_timer, NULL);
   #endif
   ```
   Same approach for synthetic touch, sensor data, etc.

#### Iteration loop

For pure UI work the loop is:

```bash
# Edit ui_screen_foo.c
uv run west build -d builds/foo_sim
./build/foo_sim/zephyr/zephyr.exe
```

That's ~2-5 s round trip vs ~15 s for `west build && west flash &&
reset`. Worth the up-front investment of getting the sim build to
compile.

#### Limitations of native_sim

- No real display drivers, just SDL. Visual fidelity is good but
  refresh timing differs from a hardware LCD.
- Touch is a mouse click (LVGL maps SDL pointer events to indev). Good
  for "does my callback fire" but not for testing real touch latency.
- File-system, networking, threading, kernel APIs are all real and
  work as on hardware — this isn't a "fake" port, it's Zephyr running
  on POSIX with a different SoC layer.
- Memory limits are host-driven, so you won't catch heap-exhaustion
  bugs in sim that you'd see on a tight MCU.

---

### <a name="bugs"></a>Common bugs and gotchas

#### Negative numbers truncate the wrong way in formatting

`(int)(-7) / 10 == 0` in C (truncates toward zero), so naive
`snprintf("%d.%d", t/10, abs(t)%10)` for a 0.1-degree fixed-point
value of `-7` renders as `0.7` instead of `-0.7`. Always render the
sign explicitly:

```c
int abs_t = t < 0 ? -t : t;
snprintf(buf, sizeof(buf), "%s%d.%d", t < 0 ? "-" : "", abs_t / 10, abs_t % 10);
```

#### Recolor format strings need the alpha byte masked

`lv_color_to_u32` returns `uint32_t` with the alpha byte set. The
`#RRGGBB ...#` recolor parser expects exactly six hex digits. Mask:

```c
lv_label_set_text_fmt(lbl, "#%06x text#",
                      (unsigned)lv_color_to_u32(c) & 0xFFFFFFu);
```

Without the mask the parser sees eight digits and either renders the
literal "#" markers or picks a wrong color.

#### Static globals work; threading rules matter more

LVGL widgets are essentially singletons in many embedded UI apps;
keeping `static lv_obj_t *` handles per screen module is fine and is
the convention used throughout `applications/beta_hri/src/ui/`. Don't
panic-refactor into "containers" until you have a reason — the real
discipline is keeping all `lv_*` calls on the LVGL thread.

#### `lv_scr_load` while a separate `_update` runs is fine

`_update` writes through retained widget pointers regardless of which
screen is currently in the foreground; LVGL only paints the active
screen, so off-screen mutations are cheap and don't cause visual
artifacts.

#### Pre-build guards

If your `_tick` or `_update` runs on a timer that may fire before
`_build`, guard with the screen handle:

```c
if (chart[0] == NULL) return;   /* not built yet */
```

Static-storage zero-init makes this reliable without explicit init.

---

### <a name="troubleshooting"></a>Troubleshooting

#### Symbols defined but screen never appears

Check, in this order:

1. Did `_build` actually run? Add a `LOG_INF("battery built")` in it.
2. Did `lv_scr_load` get called for the new screen? Log
   `model.active_screen` in your `handle_ui_cmd` switch.
3. Are there competing publishers on the screen-command channel? Grep
   for `zbus_chan_pub(&ui_cmd_chan` — should be exactly one place
   under normal operation.
4. Did the binary actually flash? `nm builds/<app>/zephyr/zephyr.elf
   | grep ui_screen_foo` should show all four functions and the
   `static` screen handle.

#### Screen renders but widgets are missing / clipped

Default `lv_obj_create` containers have a panel style with padding,
border, and scrollable enabled. For a clean container:

```c
lv_obj_remove_style_all(strip);          /* nuke default panel chrome */
lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
```

If a label seems to be drawing outside its parent, it's probably
overflow-wrapping; set `LV_LABEL_LONG_CLIP` (or
`LV_LABEL_LONG_SCROLL` if you want a marquee) and a finite width.

#### Build succeeds but the chart is invisible

Common causes:
- `CONFIG_LV_USE_CHART=y` missing → chart functions are stubs.
- Chart height is `LV_SIZE_CONTENT` and the parent flex column
  doesn't have a defined height for the chart to expand into. Set an
  explicit pixel height on chart widgets.
- Y-axis range collapsed (e.g. `set_axis_range(c, _, 0, 0)`) → all
  points clip to the bottom of the chart and become invisible.

#### Crashes after some uptime — pool exhaustion

LVGL silently returns NULL from `lv_*_create` on pool exhaustion. Add
heap watch:

```c
#include <lvgl_mem.h>
LOG_INF("lv pool free=%u", lv_get_dynamic_buf_size());
```

(or check `mem_pool_stats` in your app's diagnostic command). Bump
`CONFIG_LV_Z_MEM_POOL_SIZE` accordingly.

#### Multiple buttons / observers cause phantom screen advances

If a single press advances two screens, audit `ZBUS_OBSERVERS(...)`
on your button channel. A common cause is leftover screen-toggling
code in another module (e.g. an `app_controller` that subscribed to
`button_events_chan` and still flips a stale local screen variable on
release). Solution: remove the dead observer and let exactly one
consumer drive screen navigation.

#### touch indev fires phantom events

Some controllers report a stray touch at boot, at coordinates
`(0,0)`, before they're fully initialised. If your `(0,0)` widget
happens to be clickable, you get a phantom click on first paint. Add
a "discard first touch" guard in the indev callback or move
clickable widgets out of the top-left corner during bring-up.

### See also

- `zephyr-kconfig` — broader Kconfig debugging (unmet deps, hidden
  symbols).
- `zephyr-debugging` — capturing boot logs and shell-driven
  introspection of a running device.
- `zephyr-testing` — using Twister and Ztest for headless UI logic
  tests when `native_sim` rendering isn't enough.
- `zephyr-kernel-datapassing` — zbus, msgq, fifo for getting data
  from worker threads to the UI thread without crossing the
  LVGL-thread boundary.

---

## Trap: LVGL's monochrome path hangs boot on SSD1306 (verified)

On an ESP32 + SSD1306 128x32 mono OLED (the Waveshare `ros_driver` board),
driving the panel through LVGL **hangs during early init** — at POST_KERNEL,
*before* the Zephyr boot banner prints. The serial signature is: MCUboot output,
then two ANSI colour codes (the shell prompt starting), then silence. It is not a
reset loop, so it doesn't look like a crash.

Things that do **not** fix it:

- `CONFIG_LV_COLOR_DEPTH_1=y` is *required* for any mono attempt (without it the
  Zephyr glue leaves `mono_conv_buf` NULL and faults in `lvgl_display_mono.c`),
  but it is not sufficient.
- Matching the in-tree `ssd1306_128x32` shield defconfig
  (`CONFIG_LV_Z_COLOR_MONO_HW_INVERSION=y`, `CONFIG_LV_Z_VDB_SIZE=64`) still
  hangs. The hang is inside LVGL's own mono init/render path.

**Use the Character Frame Buffer instead of LVGL for this panel:**

```kconfig
CONFIG_DISPLAY=y
CONFIG_CHARACTER_FRAMEBUFFER=y
# no LVGL
```

CFB has no render loop or work queue — just draw and
`cfb_framebuffer_finalize()` — and allocates a single 512-byte buffer. Built-in
fonts are 10x16 / 15x24 / 20x32, so on 128x32 the 10x16 font gives 2 rows of
about 12 characters. (Practical consequence: split a dotted-quad IP address after
the second dot to make it fit.)

The SSD1306 *driver* itself is fine; only the LVGL layer hangs. Two projects in
this workspace hit this — `pt_control` ships the CFB path and boots reliably,
`rasprover` ships with the OLED disabled.
