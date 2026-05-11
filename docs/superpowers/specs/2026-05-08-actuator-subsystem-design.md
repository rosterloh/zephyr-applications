# Generic actuator subsystem (rosterloh-drivers)

**Status:** design
**Date:** 2026-05-08
**Target repo:** [`rosterloh/zephyr-drivers`](https://github.com/rosterloh/zephyr-drivers) (consumed via `deps/modules/lib/rosterloh-drivers` in this workspace)
**Consumers:** `applications/motor_controller` (v1), `applications/rasprover` (later), future FOC application

## Background

`applications/motor_controller/src/main.c` drives Dynamixel smart servos directly through the `dxl_*` protocol API: discovering motors with `dxl_motor_get`, configuring `OPERATING_MODE` / `TORQUE_ENABLE` / `PROFILE_VELOCITY` by raw register, issuing `dxl_sync_write_u32(GOAL_POSITION, ...)` per slider tick, and polling `PRESENT_POSITION` / `PRESENT_TEMPERATURE` / `HARDWARE_ERROR_STATUS` from a periodic worker. This works but leaks the Dynamixel control table into application code, and the same shape will repeat in `rasprover` and a future FOC application unless we abstract it.

Two upstream/community efforts inform the design:

- **Zephyr PR [#98741](https://github.com/zephyrproject-rtos/zephyr/pull/98741)** — proposes an `actuator` *driver class* with a sync `actuator_set_setpoint(dev, q31_t)` API. Q31 in [-1, +1]. No mode, no feedback, no async. Synchronous only. PWM-servo backend included; smart-servo feedback path is an open argument.
- **[FreedomVeiculosEletricos/zephyr-motion-control](https://github.com/FreedomVeiculosEletricos/zephyr-motion-control/tree/feature/implementation_1)** — full motion-control *subsystem* with 6-state machine (UNINIT→IDLE→ALIGN→RUN→STOP/STO/FAULT), multi-rate scheduling, motor groups, IEC 61508 STO. POC; implementation pending.
- **[teslabs/spinner](https://github.com/teslabs/spinner)** — FOC firmware that decomposes a motor controller into three orthogonal driver classes (`currsmp`, `feedback`, `svpwm`) plus a `lib/control/cloop.c` that composes them. Pure math (`lib/svm`) is hardware-independent and unit-testable.

The Zephyr PR is too narrow for our hardware (smart servos with rich feedback and modes). The FreedomVeiculos design is too heavy (multi-rate scheduling and STO are FOC-specific concerns we don't need at the subsystem layer). Spinner's compositional approach informs the *internal* shape of the FOC backend we'll add later.

## Goals

1. Single API for commanding heterogeneous actuators (Dynamixel smart servos, H-bridge DC motors, future FOC) from application code.
2. Typed, SI-unit setpoints (`actuator_set_position(dev, rad)`, `actuator_set_velocity(dev, rad_s)`, `actuator_set_effort(dev, Nm)`).
3. First-class group operations that collapse to a single bus transaction when the backend supports it (Dynamixel SYNC_WRITE / SYNC_READ).
4. Feedback via callback now, with the vtable shaped so an RTIO `submit` op can be added later without breaking callers.
5. Subsystem-owned state machine and fault policy; app-overridable.
6. Shell support from v1 for bring-up.

## Non-goals

- RTIO support in v1 (vtable slot reserved; deferred).
- FOC backend implementation in v1 (architecture sketched; no code).
- PWM hobby-servo backend (no current consumer).
- Multi-rate scheduling, STO, IEC 61508 features.
- Dynamic group construction at runtime (groups are static via `ACTUATOR_GROUP_DEFINE`).
- Settings persistence for tuning. Bring-up via shell, hardcode in DT.

## Architecture

```
┌──────────────────────────────────────────────────────┐
│ application code (motor_controller, rasprover, foc)  │
│   actuator_set_position(dev, 1.57f);                 │
│   actuator_group_set_velocity(grp, vels);            │
│   actuator_register_state_cb(dev, on_state, ud);     │
└─────────────────────────┬────────────────────────────┘
                          │  public API: include/zephyr/actuator/*.h
┌─────────────────────────┴────────────────────────────┐
│ subsys/actuator/                                     │
│  • state machine: DISABLED / READY / ALIGNING /      │
│                   ACTIVE / FAULT                     │
│  • capability validation (mode supported? in range?) │
│  • group registry & dispatch (single-backend fast    │
│    path → driver group_op; mixed → loop)             │
│  • fault policy dispatcher (per-actuator + per-group)│
│  • callback fan-in (state, feedback)                 │
│  • SI ↔ raw conversion lives in driver, but subsys   │
│    owns the SI contract                              │
└─────────────────────────┬────────────────────────────┘
                          │  backend vtable (DEVICE_API)
┌──────────┬──────────────┴───────────────┬────────────┐
│ dxl bkd  │ hbridge bkd                  │ foc bkd    │
│ (uses    │ (PWM + GPIO dir + ADC for    │ (later;    │
│ dxl_*    │  current, optional encoder)  │ runs FOC   │
│ proto)   │                              │ in PWM ISR)│
└──────────┴──────────────────────────────┴────────────┘
       │              │                          │
       ▼              ▼                          ▼
   dynamixel      pwm/gpio/adc                pwm/adc
   driver         + qdec                      + qdec/hall
```

**Layering rationale.** Subsystem owns concerns common to every actuator (state, capability check, group dispatch, fault policy). Drivers own per-backend transport, unit conversion (SI ↔ raw), and any backend-specific features. Pure logic that has no `struct device` dependencies (state-machine transitions, capability matching, NaN-safe limit clamping, unit-conversion helpers) lives in `lib/actuator/` so it's testable on `native_sim` without any driver instance — directly informed by spinner's `lib/svm` pattern.

**Why subsystem and not just a driver class.** With FOC on the roadmap, a state machine becomes load-bearing rather than ceremony: `ALIGNING` is a real phase for sensorless/Hall BLDC, and structured fault recovery (clear → re-arm → re-enable) needs to be consistent across heterogeneous actuators in a robot. Group enable barriers and fault propagation are written-once-or-written-wrong-N-times. The subsystem stays trimmed: no multi-rate scheduling, no FOC-specific HOT/COLD ISR data layout — those belong inside the FOC backend.

**Why not RTIO yet.** The Dynamixel transport (`dxl_send_recv`) is half-duplex blocking serial; RTIO-native means converting to async UART, owning direction-switching in the ISR, and tracking in-flight transactions per iface. Realistically 1–2 weeks of focused rework on a driver that was just cleaned up. Upstream PR #98741 is sync-only, so adopting RTIO means owning the divergence. Crucially, RTIO buys nothing for FOC's hot path — the current loop runs in the PWM-update ISR, where RTIO can't reach. For 100 Hz robot control loops, callbacks are equally good. The vtable reserves a `submit` slot; v1 backends leave it `NULL`.

## Public API

Three headers under `include/zephyr/actuator/`. Path is chosen to match Zephyr conventions; the headers are installed by the rosterloh-drivers module.

### `actuator_types.h` — shared types

```c
enum actuator_mode {
    ACTUATOR_MODE_DISABLED = 0,
    ACTUATOR_MODE_POSITION,    /* setpoint in radians */
    ACTUATOR_MODE_VELOCITY,    /* setpoint in rad/s   */
    ACTUATOR_MODE_EFFORT,      /* setpoint in N·m     */
};

#define ACTUATOR_CAP_POSITION       BIT(0)
#define ACTUATOR_CAP_VELOCITY       BIT(1)
#define ACTUATOR_CAP_EFFORT         BIT(2)
#define ACTUATOR_CAP_NEEDS_ALIGN    BIT(3)   /* FOC sensorless / Hall align */
#define ACTUATOR_CAP_GROUP_NATIVE   BIT(4)   /* backend has a real group_op */
#define ACTUATOR_CAP_FAULT_LATCHING BIT(5)   /* faults need explicit clear */

enum actuator_state {
    ACTUATOR_STATE_DISABLED = 0,
    ACTUATOR_STATE_READY,      /* enabled, holding 0 effort */
    ACTUATOR_STATE_ALIGNING,   /* FOC pre-run only */
    ACTUATOR_STATE_ACTIVE,     /* tracking a setpoint */
    ACTUATOR_STATE_FAULT,
};

#define ACTUATOR_FAULT_OVERCURRENT  BIT(0)
#define ACTUATOR_FAULT_OVERTEMP     BIT(1)
#define ACTUATOR_FAULT_OVERVOLTAGE  BIT(2)
#define ACTUATOR_FAULT_UNDERVOLTAGE BIT(3)
#define ACTUATOR_FAULT_OVERLOAD     BIT(4)
#define ACTUATOR_FAULT_COMM         BIT(5)
#define ACTUATOR_FAULT_DRIVER(n)    BIT(16 + (n))  /* driver-specific bits */

#define ACTUATOR_FB_POSITION    BIT(0)
#define ACTUATOR_FB_VELOCITY    BIT(1)
#define ACTUATOR_FB_EFFORT      BIT(2)
#define ACTUATOR_FB_TEMPERATURE BIT(3)

struct actuator_feedback {
    uint32_t valid_mask;       /* ACTUATOR_FB_* — which fields are populated */
    float position;            /* rad   */
    float velocity;            /* rad/s */
    float effort;              /* N·m   */
    float temperature;         /* °C; NaN if not reported */
    uint32_t fault_flags;
    uint64_t timestamp_us;     /* k_uptime_get() at sample time */
};

struct actuator_limits {
    float min_position, max_position;  /* rad; NaN = unbounded */
    float max_velocity;                /* rad/s; NaN = backend default */
    float max_effort;                  /* N·m */
};
```

**Effort unit decision.** All backends accept N·m at the API layer. Current-controlled backends (FOC; Dynamixel current mode) convert via a per-instance DT property `torque-constant-mnm-per-a`. Apps stay portable across actuators; the backend owns the model-specific scaling.

**Why `float` not Q31.** SI units make Q31 awkward (0.95 N·m doesn't fit a normalised range nicely without exposing a hardware-range constant). FOC and other tight-loop backends will run on M4F+/M7 class hardware with hardware float; on softer cores (M0+) the control-loop math runs at lower rates where soft-float cost is acceptable. The Dynamixel backend converts to ticks internally; H-bridge converts to duty. PR #98741's Q31 design is preserved in spirit (a single normalised setpoint exists conceptually as Q31 ≈ effort/max_effort) but isn't the API.

### `actuator.h` — per-device API

```c
__syscall int actuator_enable(const struct device *dev);
__syscall int actuator_disable(const struct device *dev);
__syscall int actuator_clear_fault(const struct device *dev);

__syscall enum actuator_state actuator_get_state(const struct device *dev);
__syscall uint32_t actuator_get_capabilities(const struct device *dev);

/* Typed setpoints. Implicit DISABLED → READY → ACTIVE if needed.
 * Returns -ENOTSUP if the mode is not in the driver's capability mask. */
__syscall int actuator_set_position(const struct device *dev, float rad);
__syscall int actuator_set_velocity(const struct device *dev, float rad_s);
__syscall int actuator_set_effort(const struct device *dev, float nm);

/* Synchronous read — forces a backend transaction. */
__syscall int actuator_read_feedback(const struct device *dev,
                                     struct actuator_feedback *out);

/* Cached read — returns last sample populated by the driver-internal worker.
 * No bus traffic. Apps that already have a feedback callback registered can
 * use this from inside it as a no-op. */
__syscall int actuator_get_feedback(const struct device *dev,
                                    struct actuator_feedback *out);

__syscall int actuator_set_limits(const struct device *dev,
                                  const struct actuator_limits *limits);

typedef void (*actuator_state_cb_t)(const struct device *dev,
                                    enum actuator_state new_state,
                                    void *user_data);
typedef void (*actuator_feedback_cb_t)(const struct device *dev,
                                       const struct actuator_feedback *fb,
                                       void *user_data);

int actuator_register_state_cb(const struct device *dev,
                               actuator_state_cb_t cb, void *user_data);
int actuator_register_feedback_cb(const struct device *dev,
                                  actuator_feedback_cb_t cb, void *user_data);
```

**Implicit state promotion.** `actuator_set_position` from DISABLED enables, then sets. This matches the "I just want to command it" common case. Apps that need explicit control (e.g. enable barrier across a group) call `actuator_enable` first.

**Two-tier read API.** `read_feedback` triggers a backend transaction (Dynamixel SYNC_READ, ADC sample, etc.); `get_feedback` returns the last cached snapshot from the driver's periodic worker. Apps choose based on freshness needs. The cached path is `O(1)` and allocates no bus time.

**Feedback cadence.** The driver runs a `k_work_delayable` at the DT-configured `update-period-ms`, populates the cache, and fires registered feedback callbacks. State callbacks fire immediately on transition (from the same context that detected it — usually the worker, but FAULT may also fire from a setpoint call's error path).

**Callback context.** Both feedback and state callbacks run from the system workqueue (where the driver's periodic worker lives) or a setpoint caller's thread context — never from an ISR. Callbacks must not block on actuator API calls for the same device (would deadlock the worker); calling into a *different* actuator is allowed.

### `actuator_group.h` — multi-device dispatch

```c
struct actuator_group_data;     /* internal mutable state */

struct actuator_group {
    const struct device * const *devs;
    size_t n;
    struct actuator_group_data *data;
};

#define ACTUATOR_GROUP_DEFINE(name, ...)                                 \
    static const struct device *_##name##_devs[] = { __VA_ARGS__ };      \
    static struct actuator_group_data _##name##_data;                    \
    static const struct actuator_group name = {                          \
        .devs = _##name##_devs,                                          \
        .n = ARRAY_SIZE(_##name##_devs),                                 \
        .data = &_##name##_data,                                         \
    }

int actuator_group_enable(const struct actuator_group *grp);
int actuator_group_disable(const struct actuator_group *grp);
int actuator_group_clear_fault(const struct actuator_group *grp);

/* Atomic where possible (single-backend fast path); loop fallback otherwise.
 * Length of values[] must equal grp->n. */
int actuator_group_set_position(const struct actuator_group *grp,
                                const float rad[]);
int actuator_group_set_velocity(const struct actuator_group *grp,
                                const float rad_s[]);
int actuator_group_set_effort(const struct actuator_group *grp,
                              const float nm[]);

/* Single-transaction read where supported (Dynamixel SYNC_READ). On partial
 * failure returns -EIO with per-member valid_mask = 0 for the failed slots. */
int actuator_group_read_feedback(const struct actuator_group *grp,
                                 struct actuator_feedback fb[]);

enum actuator_group_fault_policy {
    ACTUATOR_GROUP_POLICY_ISOLATE = 0,    /* default: faulted member only */
    ACTUATOR_GROUP_POLICY_DISABLE_ALL,
    ACTUATOR_GROUP_POLICY_ESTOP,          /* DISABLE_ALL + latch */
};
int actuator_group_set_fault_policy(const struct actuator_group *grp,
                                    enum actuator_group_fault_policy policy);
```

**Group is a static struct, not an opaque handle.** No allocator, no init function — `ACTUATOR_GROUP_DEFINE` puts the device-pointer array and the mutable bookkeeping (`_data`) in static storage. Callable from device init. The dispatcher inspects member backends on first use to decide the fast-path vs loop strategy.

**Dispatch fast path.** When all `grp->devs` share the same `device_api` and that api implements `group_set_setpoints`, the subsystem calls it once with the full vector. The Dynamixel backend implements this as a single SYNC_WRITE for the same bus. When group members span backends, the subsystem falls back to a per-device loop. Capability `CAP_GROUP_NATIVE` is informational only — the dispatcher checks the function pointer at runtime.

**Atomicity caveat.** "Atomic" means "single bus transaction" — once the bus accepts the broadcast, individual servos commit independently. SYNC_WRITE has no per-servo status, so partial failure is silent. The next `group_read_feedback` exposes any divergence. This is documented in the header.

## State machine

Owned by the subsystem. Drivers report transitions via internal `actuator_report_state(dev, new_state, fault_flags)` (declared in `drivers/actuator/actuator_internal.h`, not public).

```
                ┌──────────┐
                │ DISABLED │  (reset / cold boot)
                └────┬─────┘
       enable        │       disable
   ┌────────────►────┴────────◄──────────┐
   │                                      │
   │     ┌────────┐  needs_align?  ┌──────┴──────┐
   ├─────► READY  ├──────yes──────► ALIGNING     │
   │     └───┬────┘                └──────┬──────┘
   │         │                            │ aligned
   │  setpoint│         ┌──────────────────┘
   │   call  │          │
   │     ┌───▼──────────▼──┐
   │     │     ACTIVE      │
   │     │ (tracking sp)    │
   │     └─────────┬───────┘
   │   fault       │       fault from any state
   │  ┌────────────┘            │
   │  ▼                         │
   │ ┌────────┐  clear_fault    │
   └─┤  FAULT │◄────────────────┘
     └────────┘  → DISABLED
```

**Rules.**

- Subsystem rejects setpoint calls while FAULT (returns `-EPERM`) and rejects mode-mismatched calls (`-ENOTSUP`).
- `clear_fault` is meaningful only when the driver advertises `CAP_FAULT_LATCHING`. For non-latching backends, faults auto-clear on the next clean feedback read; calling `clear_fault` is a no-op (returns 0).
- `ALIGNING` is entered automatically by the subsystem after `enable` if the driver advertises `CAP_NEEDS_ALIGN`. The driver completes alignment in its worker and reports `READY` (or `FAULT`). Setpoint calls block via `-EAGAIN` until alignment completes; apps poll state or wait on the state callback.
- Backends without `CAP_NEEDS_ALIGN` skip ALIGNING entirely and go `DISABLED → READY → ACTIVE` on first setpoint.

## Driver vtable

Declared in `drivers/actuator/actuator_internal.h`:

```c
struct actuator_driver_api {
    int (*enable)(const struct device *dev);
    int (*disable)(const struct device *dev);
    int (*clear_fault)(const struct device *dev);

    int (*set_setpoint)(const struct device *dev,
                        enum actuator_mode mode, float value);

    int (*read_feedback)(const struct device *dev,
                         struct actuator_feedback *out);

    int (*set_limits)(const struct device *dev,
                      const struct actuator_limits *limits);

    /* Optional: same-backend group fast path. Subsystem confirms all
     * members share api before calling. NULL = use loop fallback. */
    int (*group_set_setpoints)(const struct device * const *devs, size_t n,
                               enum actuator_mode mode, const float *values);
    int (*group_read_feedback)(const struct device * const *devs, size_t n,
                               struct actuator_feedback *out);

    /* Reserved for v2 RTIO. NULL in v1. */
    /* int (*submit)(const struct device *dev, struct rtio_iodev_sqe *sqe); */
};
```

**Why one `set_setpoint` op instead of three.** Drivers usually share most of the per-mode bookkeeping (range check, last-commanded cache, callback fire); a single op with a `mode` discriminator keeps that consolidated. The subsystem's `actuator_set_position` etc. are thin wrappers that fill in the mode.

**Why a separate group op rather than always looping.** The `dxl_sync_write_u32` saving is real (one bus transaction vs N): for an 8-motor arm at 1 kHz it's the difference between feasible and not. The vtable hook lets backends opt in without burdening backends that don't benefit.

## Devicetree bindings

Under `dts/bindings/actuator/`.

### `rosterloh,actuator-dxl.yaml`

```yaml
description: |
  Smart-servo actuator backed by the rosterloh dynamixel driver. Each instance
  is a child of a robotis,dynamixel bus node; the subsystem discovers the
  parent iface via DT_BUS at compile time.

compatible: "rosterloh,actuator-dxl"

include: [actuator-common.yaml]   # update-period-ms, label, default-mode

properties:
  reg:
    type: int
    required: true
    description: Dynamixel bus ID (1..253).

  torque-constant-mnm-per-a:
    type: int
    description: |
      Per-model torque constant in milli-N·m per Ampere. Required if effort
      mode is enabled. e.g. XL330-M288 ≈ 320.

  position-min-rad-milli:
    type: int
    description: Minimum position in milliradians. Omit for unbounded.
  position-max-rad-milli:
    type: int

  gear-ratio-num:
    type: int
    default: 1
  gear-ratio-den:
    type: int
    default: 1

  profile-velocity:
    type: int
    description: |
      Optional Dynamixel PROFILE_VELOCITY register value applied at init.
      Backend-specific escape hatch for tuning without exposing the control
      table to applications.
```

### `rosterloh,actuator-hbridge.yaml`

```yaml
description: H-bridge DC motor with optional encoder and current sense.

compatible: "rosterloh,actuator-hbridge"

include: [actuator-common.yaml]

properties:
  pwms:
    type: phandle-array
    required: true

  dir-gpios:
    type: phandle-array
    required: true

  io-channels:
    type: phandle-array
    description: |
      Optional ADC channel for current sense. Enables CAP_EFFORT and
      ACTUATOR_FAULT_OVERCURRENT detection.

  encoder:
    type: phandle
    description: |
      Optional phandle to a qdec node. Enables CAP_POSITION and CAP_VELOCITY.

  torque-constant-mnm-per-a:
    type: int
  max-current-ma:
    type: int
```

### `actuator-common.yaml` (include)

```yaml
properties:
  default-mode:
    type: string
    enum: ["position", "velocity", "effort"]
    default: "position"
  update-period-ms:
    type: int
    default: 100
  label:
    type: string
```

### Example: motor_controller overlay

```dts
&dxl_bus {
    motor0: motor@1 {
        compatible = "rosterloh,actuator-dxl";
        reg = <1>;
        default-mode = "position";
        torque-constant-mnm-per-a = <320>;
        position-min-rad-milli = <(-3141)>;
        position-max-rad-milli = <3141>;
        update-period-ms = <100>;
        label = "shoulder";
    };
    motor1: motor@2 {
        compatible = "rosterloh,actuator-dxl";
        reg = <2>;
        torque-constant-mnm-per-a = <320>;
        label = "elbow";
    };
};
```

**Why DT child-of-bus.** Lets `actuator_dxl.c` discover its parent iface with `DEVICE_DT_GET(DT_BUS(node))`, mirroring Zephyr's I²C/SPI child pattern. Eliminates the `dxl_iface_get_by_name` step in app code.

## Fault policy

**Per-actuator default.** Any non-zero `fault_flags` from the driver triggers `state → FAULT`, fires the state callback, and (for backends without `CAP_FAULT_LATCHING`) auto-recovers to `READY` on the next clean feedback cycle.

**Per-group.** The group dispatcher inspects member states after every command and feedback read. On any member fault, applies the group's policy:

- `ISOLATE` (default): only the faulted member transitions; siblings continue.
- `DISABLE_ALL`: all members go to DISABLED; their state callbacks fire.
- `ESTOP`: DISABLE_ALL plus latching — the group rejects new commands until `actuator_group_clear_fault` is called.

**App opt-in for stricter handling.** An app's state callback runs after the subsystem policy. The app sees consistent default behaviour even if it does nothing; it can layer additional logic (e.g. notify the operator, switch to a degraded mode) without re-implementing the basics.

## Shell

`subsys/actuator/actuator_shell.c`, gated by `CONFIG_ACTUATOR_SHELL`. Pattern matches spinner's `cloop_shell.c` and the existing `dynamixel` shell.

```
actuator list
actuator <name> enable | disable | clear-fault
actuator <name> set position <rad>
actuator <name> set velocity <rad_s>
actuator <name> set effort <nm>
actuator <name> get state
actuator <name> get feedback
actuator <name> get caps
actuator group <name> enable | disable | clear-fault
actuator group <name> set position <r0> <r1> ...
actuator group <name> get feedback
```

Cost: ~200 LoC. Pays for itself the first time you bring up a new motor without writing a test app.

## File layout

```
include/zephyr/actuator/
  actuator.h
  actuator_group.h
  actuator_types.h

drivers/actuator/
  actuator_internal.h         (vtable, report_state, common worker helpers)
  Kconfig
  CMakeLists.txt
  dxl/
    Kconfig
    actuator_dxl.c
  hbridge/
    Kconfig
    actuator_hbridge.c
  fake/
    Kconfig                   (CONFIG_ACTUATOR_FAKE; for tests)
    actuator_fake.c
  /* foc/  (later) */

subsys/actuator/
  Kconfig
  CMakeLists.txt
  actuator.c                  (state machine, public API impl)
  actuator_group.c            (dispatch, fault policy)
  actuator_shell.c            (CONFIG_ACTUATOR_SHELL)
  actuator_callbacks.c        (sys_slist + dispatch)

lib/actuator/
  Kconfig
  CMakeLists.txt
  state_machine.c             (transition table; no struct device)
  capabilities.c              (mode/cap matching)
  unit_helpers.c              (rad↔ticks templates, NaN-safe limit clamp)

dts/bindings/actuator/
  actuator-common.yaml
  rosterloh,actuator-dxl.yaml
  rosterloh,actuator-hbridge.yaml

samples/actuator/
  basic/                      (single-actuator hello)
  group_dxl/                  (multi-dxl group; ports motor_controller's logic)

tests/
  lib/actuator/               (twister; native_sim; pure-logic tests)
  drivers/actuator/dxl/       (twister; uart-emul)
  drivers/actuator/hbridge/   (twister; pwm-emul/gpio-emul)
  subsys/actuator/            (twister; fake driver; state machine + group)
```

## FOC backend (future, sketch only)

Not part of v1. Documented here so v1 doesn't lock decisions that prevent it. Architecture follows spinner's compositional pattern: the actuator API doesn't change, but internally the backend is composed from three orthogonal driver classes plus a control library.

```
drivers/actuator/foc/
  actuator_foc.c              (binds the actuator API to the trio below)
  Kconfig

drivers/foc_currsmp/
  currsmp_shunt_stm32.c       (3-phase shunt currents read in PWM ISR)
drivers/foc_feedback/
  feedback_halls_stm32.c
  feedback_qdec.c             (uses Zephyr qdec)
drivers/foc_svpwm/
  svpwm_stm32.c

lib/foc/
  svm.c                       (space-vector modulation; numpy-compared tests)
  cloop.c                     (Park/Clarke + PI; numpy-compared tests)

dts/bindings/actuator/
  rosterloh,actuator-foc.yaml (phandles to currsmp, feedback, svpwm + motor
                               params: Rs, Ls, pole-pairs, torque-constant)
```

From outside, an FOC actuator is just another `actuator` device with capabilities `POSITION | VELOCITY | EFFORT | NEEDS_ALIGN`. Inside, `actuator_foc.c` runs the current loop in the PWM-update ISR (where RTIO can't reach — confirmed by spinner's design), with speed/position outer loops on a high-priority k_timer-driven thread.

**Constraints v1 must respect:**

- Don't assume one DT node per actuator — the FOC binding uses phandles to component sub-drivers.
- Don't assume the feedback worker drives the PWM cadence — FOC's PWM cadence is set by the timer, not `update-period-ms`.
- Don't assume `set_setpoint` is fast — for FOC it just updates a target the ISR consumes; the call is decoupled from the loop rate.

## Testing

Three layers, all twister:

1. **`tests/lib/actuator/`** (`native_sim`). Pure logic, no `struct device`: state machine transitions for every legal/illegal pair, capability matcher, NaN-safe limit clamping, group-dispatch decision table. Fastest cycle; runs on every commit.
2. **`tests/subsys/actuator/`** (`native_sim` + fake driver). End-to-end public API: enable/disable, mode rejection, group dispatch fast path vs loop fallback, fault policy enforcement, callback dispatch ordering.
3. **`tests/drivers/actuator/dxl/`** (`native_sim` + `uart-emul`). Mirrors existing `tests/drivers/dynamixel/`. Verifies `set_position(π/2)` on an XL330 produces `dxl_write_u32(GOAL_POSITION, 2048 + offset)`. H-bridge gets the parallel treatment with `pwm-emul`/`gpio-emul`.

Twister entries align with the existing rosterloh-drivers layout (`tests/drivers/dynamixel/`, `tests/drivers/seesaw/`); no new infrastructure needed.

## Migration plan for `applications/motor_controller`

Done in the same PR that adds the subsystem so changes co-evolve. Motor_controller is the v1 reference consumer.

1. Add `compatible = "rosterloh,actuator-dxl"` children under the existing `dxl_bus` overlay node — one per motor currently discovered at runtime, with `torque-constant-mnm-per-a`, default-mode, and label.
2. Replace `main.c`'s discovery loop, raw register writes (`OPERATING_MODE`, `TORQUE_ENABLE`, `PROFILE_VELOCITY`), and SYNC_WRITE/SYNC_READ calls with `actuator_*` and `actuator_group_*` calls.
3. The telemetry `k_work_delayable` becomes a single `actuator_register_feedback_cb` per motor (or one on the group). The driver-internal worker now owns the cadence.
4. Keep direct `dxl_*` access available — the actuator-dxl driver uses it under the hood. Backend-specific tweaks (`PROFILE_VELOCITY`) move to DT properties or stay as `dxl_*` calls during init. No need to delete the protocol API.

Net: ~150 LoC of register-shuffling out of `main.c`, replaced by ~30 LoC of actuator API calls. LVGL/UI code unchanged.

## Open questions

- **Group-level enable barrier.** Should `actuator_group_enable` synchronously wait for all members to reach READY before returning? For Dynamixel this is fast (a single SYNC_WRITE of TORQUE_ENABLE); for FOC it could block on alignment for hundreds of milliseconds. Default v1: synchronous wait with a configurable timeout, returning `-ETIMEDOUT` on fail. Revisit if the FOC backend makes this painful.
- **Per-motor `torque-constant` vs per-model defaults.** Required-on-DT keeps things explicit but is repetitive when a robot has eight identical XL330-M288s. Possible compromise: include files (`xl330-m288.dtsi`) shipped with the dxl backend. Defer until pain emerges.
