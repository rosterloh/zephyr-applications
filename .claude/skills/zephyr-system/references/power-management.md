# Zephyr Power Management

## Overview

Use this reference when the goal is to reduce power without breaking
responsiveness. Power management in Zephyr is usually a combination of:

- system PM through the idle-thread and policy path
- device runtime PM for individual peripherals
- correctly configured wake sources
- measurement to confirm the real current draw

Power management crosses several Zephyr areas. When needed, also read:

- `./kconfig.md` for `prj.conf`, symbol dependencies, and menuconfig debugging
- `../../zephyr-peripherals/references/devicetree.md` for wake-capable pins, interrupts, and board wiring
- `../../zephyr-peripherals/references/device-drivers.md` for GPIO wake interrupts and driver PM callbacks (`PM_DEVICE_*`)
- `../../zephyr-kernel/references/isr.md` for ISR-safe wake handling
- `../../zephyr-kernel/references/threading.md` for blocking thread design, workqueues, and scheduler behavior
- `../../zephyr-kernel/references/synchronization.md` for semaphores, events, mutexes, and wait-based coordination

## Workflow

### 1. Choose the PM level

Decide what should sleep:

- **System PM**: Use when the whole application is mostly idle and should enter low-power states automatically when no runnable work remains.
- **Device runtime PM**: Use when only specific peripherals should suspend while the rest of the system stays active.
- **Both**: Common in battery-powered products. Runtime PM trims peripheral waste while system PM handles deep idle periods.

Use the application behavior to choose the depth:

- frequent wakeups and tight latency budget: prefer lighter states
- long idle windows and interrupt-driven wakeups: allow deeper states
- thermal pressure or burst workloads: combine reduced activity with PM constraints and scheduling discipline

### 2. Enable the right Kconfig symbols

Start with the minimum configuration in `prj.conf`:

```ini
CONFIG_PM=y
CONFIG_PM_DEVICE=y
CONFIG_PM_DEVICE_RUNTIME=y
CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE=y
```

`CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE` opts *every* device into runtime PM
at init — equivalent to adding the `zephyr,pm-device-runtime-auto` property to
every devicetree node. Prefer setting that property on the specific nodes you
care about instead, unless you really want it board-wide.

**Sleep states are not Kconfig.** There is no `CONFIG_PM_STATE_*_SUPPORTED`
symbol. The states a board can enter come from devicetree `power-states` nodes
that the SoC defines and the CPU node references:

```dts
/* usually already in the SoC dtsi — check before writing your own */
&cpu0 {
    cpu-power-states = <&idle &suspend_to_ram>;
};

power-states {
    idle: idle {
        compatible = "zephyr,power-state";
        power-state-name = "suspend-to-idle";
        min-residency-us = <1000>;
        exit-latency-us = <100>;
    };
};
```

Grep the SoC dtsi for `zephyr,power-state` to see what your target actually
supports — if there are no such nodes, system PM has nothing to select and
will do nothing regardless of `CONFIG_PM`.

Also check policy-related options when the default behavior is not enough:

- `CONFIG_PM_POLICY_DEFAULT=y` for Zephyr's built-in state selection
- `CONFIG_PM_POLICY_CUSTOM=y` when the product needs custom policy logic

If a PM API seems to compile out or do nothing, confirm the controlling Kconfig symbols first.

### 3. Wire wake sources before optimizing anything else

Wake sources are the difference between a usable low-power design and a device that never resumes.

Typical wake sources:

- GPIO interrupt for a sensor, button, or card-detect line
- RTC or timer compare for periodic wakeups
- UART or other peripheral wake support, if the SoC and board expose it

Rules:

- configure the wake source before entering the target sleep state
- verify the selected pin or peripheral can wake from that state on the target SoC
- prefer interrupts over polling; polling prevents meaningful idle residency
- keep ISR work minimal and hand off real work to a thread or workqueue

### 4. Prefer policy-driven system PM, then add constraints deliberately

Zephyr system PM is normally driven by the scheduler:

1. threads block or sleep
2. the idle thread runs
3. the PM policy selects the deepest allowed state that still meets latency requirements
4. hardware enters that state and wakes on an enabled event

When a critical section must avoid deeper states, use policy locks around that window:

```c
#include <zephyr/pm/policy.h>
#include <zephyr/pm/state.h>

pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);

do_time_sensitive_exchange();

pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);
```

These locks are reference-counted. Every `lock_get()` must have a matching `lock_put()`.

If you need an app-directed state request from example code or an older codebase, verify the exact API against the Zephyr version in use before copying it. PM entry APIs have changed over time, while the idle-thread and policy model remains the stable mental model.

### 5. Design threads so they can actually go idle

Threads and semaphores are central to power management because system PM only happens when runnable work stops.

Prefer this model:

- ISR captures the wake event quickly
- ISR gives a semaphore, submits work, or sets an event
- worker thread wakes, processes the event, then blocks again
- once all threads are blocked, the scheduler can return to the idle thread and PM policy path

Good blocking primitives for low-power designs:

- `k_sem_take()` for ISR-to-thread handoff
- `k_msgq_get()` or queues/FIFOs for deferred work
- `k_event_wait()` for bitmask-style event coordination
- `k_sleep()` only when a thread truly wants timed blocking, not as a substitute for event-driven design

Avoid busy loops such as repeated polling with short sleeps. They reduce idle residency and often dominate battery drain.

Example pattern:

```c
K_SEM_DEFINE(card_sem, 0, 1);

void card_gpio_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    k_sem_give(&card_sem);
}

void card_thread(void)
{
    while (1) {
        k_sem_take(&card_sem, K_FOREVER);
        process_card_read();
        send_via_ble();
    }
}
```

This pattern is power-friendly because the thread stays blocked until real work arrives.

### 6. Use runtime PM get/put for peripherals

For application code, prefer runtime PM reference counting over manual state toggling:

```c
#include <zephyr/pm/device_runtime.h>

int rc = pm_device_runtime_get(uart_dev);
if (rc < 0) {
    return rc;
}

rc = uart_tx(uart_dev, data, len, SYS_FOREVER_MS);

int put_rc = pm_device_runtime_put(uart_dev);
if (put_rc < 0) {
    LOG_WRN("runtime put failed: %d", put_rc);
}
```

Why this pattern is preferred:

- multiple call sites can safely share the same device
- the runtime PM core handles resume and autosuspend timing
- it avoids races caused by ad hoc ACTIVE/SUSPEND transitions

Use explicit device actions mainly for driver code, tests, or diagnostics.

### 7. Keep PM callbacks fast and non-blocking

PM hooks and driver PM callbacks should only do the minimum necessary state save and restore work.

Do not:

- call blocking APIs such as `k_sleep()`
- take locks that might deadlock resume/suspend ordering
- perform long computations
- assume console logging is free during suspend/resume paths

Why `k_sleep()` is wrong here:

- `k_sleep()` is a thread-blocking API that expects normal scheduler-managed thread context
- PM hooks and driver PM callbacks run synchronously as part of the suspend/resume path
- the system cannot finish the PM transition until the callback returns
- if the callback blocks, it stalls the very path that is trying to enter or leave low power

This is usually not literal recursion. The usual failure mode is self-stall: the callback asks the scheduler for help even though the PM transition is waiting on that same callback to finish. Depending on context and Zephyr version, this may assert, fail immediately, or deadlock.

The same logic applies to blocking locks and waits. If a PM callback waits on a mutex, semaphore, or other thread-driven condition, it can create a circular wait where PM cannot complete until the callback returns, but the callback will not return until some other work runs.

Do:

- disable or re-enable expensive peripherals
- snapshot or restore minimal device state
- return quickly so wake latency stays predictable

For custom drivers, follow the PM callback patterns in `../../zephyr-peripherals/references/device-drivers.md`.

### 8. Measure before claiming success

The power meter is the ground truth. Always compare at least:

- active current
- idle current
- deep-sleep current
- wake latency
- false-wake frequency

Good measurement workflow:

1. capture baseline current before PM changes
2. enable one PM feature at a time
3. verify wake behavior still works
4. capture current again with the same test conditions

Useful tools include Nordic PPK2, Joulescope, Otii, or any lab setup with reliable current measurement.

## Design Patterns

### Interrupt-driven battery device

Use when the application mostly waits for external activity.

- configure GPIO or RTC wake source
- avoid polling loops
- wake worker threads with semaphores, events, or queues
- let the scheduler reach the idle path
- keep post-wake processing in a normal thread context

### Semaphore-driven wake pipeline

Use when an ISR should wake exactly one worker thread to do bounded work.

- ISR gives a semaphore and returns immediately
- worker thread blocks on `k_sem_take(..., K_FOREVER)`
- worker performs the real work and blocks again
- no thread spins while idle, so the system can enter PM states naturally

### Peripheral autosuspend

Use when UART, SPI, I2C, or other devices are bursty.

- call `pm_device_runtime_get()` immediately before use
- call `pm_device_runtime_put()` immediately after use
- avoid leaving devices busy forever due to missing `put()` calls

### Latency-constrained exchange

Use when a BLE exchange, flash write, or timing-sensitive sequence must not fall into a deep state.

- acquire a PM policy lock
- perform the critical exchange
- release the lock as soon as possible

## Common Pitfalls

- **Polling instead of interrupts**: the CPU never becomes idle long enough for useful residency.
- **Using threads that never block**: a single runnable polling thread can prevent system PM entirely.
- **Using semaphores or events incorrectly**: if the worker misses the handoff and falls back to polling, idle residency collapses.
- **Wrong wake assumption**: a GPIO may work as an interrupt but not as a deep-sleep wake source on the target board.
- **Forgetting a `pm_device_runtime_put()`**: the device stays resumed and wastes power.
- **Leaking PM locks**: a missing `lock_put()` silently blocks deeper states forever.
- **Using PM callbacks for heavy logic**: resume latency becomes unpredictable.
- **Ignoring device context loss**: deeper states may require full peripheral reinitialization.
- **Leaving logs or consoles enabled**: debug UART activity often dominates idle current.

## Source Locations

| Description | Path |
| :--- | :--- |
| **Power Management Docs** | `<zephyr-ws>/deps/zephyr/doc/services/pm` |
| **PM Headers** | `<zephyr-ws>/deps/zephyr/include/zephyr/pm` |
| **Kernel Docs** | `<zephyr-ws>/deps/zephyr/doc/kernel` |
| **Driver Samples** | `<zephyr-ws>/deps/zephyr/samples/drivers` |
| **Board Samples** | `<zephyr-ws>/deps/zephyr/samples/boards` |
| **Tests** | `<zephyr-ws>/deps/zephyr/tests/subsys/pm` |

`<zephyr-ws>` represents the root of the Zephyr workspace.
