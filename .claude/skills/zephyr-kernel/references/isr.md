# Interrupt Service Routines

## Overview

This skill provides expert knowledge on interrupt handling in Zephyr RTOS. It covers ISR registration (static and dynamic), direct ISRs for low-latency handling, IRQ management, interrupt context constraints, and patterns for offloading work from ISRs to threads.

### Key Constraints (Memorize)

**ISR Context Rules:**
- ISRs run on a dedicated interrupt stack
- ISRs can be nested (higher priority preempts lower)
- ISRs must **never block** - no `K_FOREVER` or `K_MSEC(n)` timeouts
- Use `k_is_in_isr()` to detect interrupt context
- Many kernel APIs are thread-only; check docs before use in ISR

**ISR-Safe Operations:**
- `k_sem_give()` - always safe
- `k_sem_take(&sem, K_NO_WAIT)` - safe (non-blocking)
- `k_msgq_put()` / `k_fifo_put()` with `K_NO_WAIT` - safe
- `k_work_submit()` - safe (offload to system workqueue)

### Workflow

#### 1. Choose ISR Type

| Type | Use When | Registration |
|------|----------|--------------|
| **Regular** | Most cases, arguments known at build time | `IRQ_CONNECT()` |
| **Dynamic** | Arguments known only at runtime | `irq_connect_dynamic()` |
| **Direct** | Ultra-low latency required | `IRQ_DIRECT_CONNECT()` |

**Step 1:** Read [#isr-types](#isr-types) for detailed implementation of each type.

#### 2. Implement ISR

Once the ISR type is chosen, implement the handler:

```c
/* Regular ISR */
void my_isr(const void *arg)
{
    /* Fast processing only - never block */
}

/* Direct ISR (for lowest latency) */
ISR_DIRECT_DECLARE(my_direct_isr)
{
    do_minimal_work();
    ISR_DIRECT_PM();  /* Power management hook */
    return 1;         /* 1 = check reschedule, 0 = skip */
}
```

#### 3. Register and Enable

```c
/* Build-time registration */
IRQ_CONNECT(IRQ_NUM, PRIORITY, my_isr, my_arg, FLAGS);
irq_enable(IRQ_NUM);

/* Runtime registration */
irq_connect_dynamic(IRQ_NUM, PRIORITY, my_isr, my_arg, FLAGS);
irq_enable(IRQ_NUM);
```

#### 4. Offload Time-Consuming Work

If ISR needs to trigger complex processing, offload to a thread.

**Step 4:** Read [#offloading](#offloading) for patterns:
- Semaphore signaling (ISR gives, thread takes)
- Work queue submission
- Message queue / FIFO handoff

#### 5. API & Configuration

For complete API signatures and Kconfig options.

**Step 5:** Read [#api](#api) for:
- All IRQ/ISR macros and functions
- Kconfig options
- Header locations

#### 6. Advanced Features

For specialized interrupt scenarios.

**Step 6:** Read [#advanced](#advanced) for:
- Zero-latency interrupts
- Shared interrupts
- Multi-level interrupt controllers
- IRQ locking patterns

### Quick Decision Guide

```
Need interrupt handling?
├── Arguments known at build time?
│   ├── Yes → IRQ_CONNECT() [most common]
│   └── No → irq_connect_dynamic()
│
├── Need ultra-low latency?
│   └── Yes → IRQ_DIRECT_CONNECT() + ISR_DIRECT_DECLARE()
│
├── Must bypass irq_lock()?
│   └── Yes → Zero-latency IRQ (ARM Cortex-M only)
│
└── Multiple ISRs on same line?
    └── Yes → Enable CONFIG_SHARED_INTERRUPTS
```

### Common Pitfalls

1. **Blocking in ISR** - Never use timeouts; always `K_NO_WAIT`
2. **IRQ_CONNECT args not const** - All args must be build-time constants
3. **Forgetting irq_enable()** - ISR won't fire without it
4. **Wrong IRQ number** - Check devicetree/board docs for correct IRQ line
5. **Priority inversion** - High-priority ISR can starve threads

### Source Locations

| Description | Path |
|:---|:---|
| **Interrupts Docs** | `<zephyr-ws>/deps/zephyr/doc/kernel/services/interrupts.rst` |
| **IRQ Header** | `<zephyr-ws>/deps/zephyr/include/zephyr/irq.h` |
| **Kernel Header** | `<zephyr-ws>/deps/zephyr/include/zephyr/kernel.h` |
| **Interrupt Tests** | `<zephyr-ws>/deps/zephyr/tests/arch/common/interrupt/` |
| **Architecture IRQ** | `<zephyr-ws>/deps/zephyr/include/zephyr/arch/<arch>/irq.h` |

*Note: `<zephyr-ws>` represents the root of the Zephyr workspace.*

## Advanced

### Table of Contents

1. [Zero-Latency Interrupts](#zero-latency-interrupts)
2. [Shared Interrupts](#shared-interrupts)
3. [Multi-Level Interrupts](#multi-level-interrupts)
4. [IRQ Locking Patterns](#irq-locking-patterns)
5. [RAM-Based Execution](#ram-based-execution)

### Zero-Latency Interrupts

Interrupts that bypass `irq_lock()` for guaranteed low latency.

#### When to Use

- Hard real-time requirements (motor control, safety systems)
- Cannot tolerate latency from kernel critical sections
- Hardware timing constraints

#### Architecture Support

Currently **ARM Cortex-M only**.

#### Requirements

- `CONFIG_ZERO_LATENCY_IRQS=y`
- Must use `IRQ_DIRECT_CONNECT` with `IRQ_ZERO_LATENCY` flag
- ISR declared with `ISR_DIRECT_DECLARE`

#### Implementation

```c
#include <zephyr/irq.h>

#define ZLI_IRQ       24
#define ZLI_PRIO      0   /* Typically highest priority */

/* Zero-latency ISR */
ISR_DIRECT_DECLARE(motor_control_isr)
{
    /* CRITICAL: No kernel API calls allowed! */

    /* Direct hardware manipulation only */
    volatile uint32_t *pwm_reg = (uint32_t *)0x40001000;
    volatile uint32_t *enc_reg = (uint32_t *)0x40002000;

    uint32_t encoder = *enc_reg;
    *pwm_reg = compute_pwm(encoder);

    /* Must return 0 - no reschedule check */
    return 0;
}

void setup(void)
{
    IRQ_DIRECT_CONNECT(ZLI_IRQ, ZLI_PRIO, motor_control_isr, IRQ_ZERO_LATENCY);
    irq_enable(ZLI_IRQ);
}
```

#### Constraints (Critical!)

| Allowed | Forbidden |
|---------|-----------|
| Direct register access | Any kernel API |
| Pure computation | `k_sem_give()` |
| Inline assembly | `k_work_submit()` |
| Static variables | `printk()` |
| | Memory allocation |
| | `ISR_DIRECT_PM()` |

**Why:** ZLI runs at priority above kernel, so kernel data structures may be in inconsistent state.

#### Communication with Threads

Use shared memory with atomic operations:

```c
#include <zephyr/sys/atomic.h>

static atomic_t zli_event_flag = ATOMIC_INIT(0);
static volatile uint32_t zli_data;

ISR_DIRECT_DECLARE(zli_isr)
{
    zli_data = read_hw();
    atomic_set(&zli_event_flag, 1);
    return 0;
}

void thread_check(void)
{
    if (atomic_cas(&zli_event_flag, 1, 0)) {
        /* Event occurred, process zli_data */
        uint32_t data = zli_data;
        process(data);
    }
}
```

### Shared Interrupts

Multiple ISRs on the same interrupt line.

#### When to Use

- Hardware routes multiple devices to one IRQ
- DMA completion + peripheral event on same line
- Legacy hardware with limited IRQ lines

#### Configuration

```conf
CONFIG_SHARED_INTERRUPTS=y
CONFIG_SHARED_IRQ_MAX_NUM_CLIENTS=4  # Max ISRs per line
```

#### Static Sharing (Build-time)

```c
#define SHARED_IRQ    24
#define SHARED_PRIO   2

void dma_isr(const void *arg)
{
    if (dma_pending()) {
        handle_dma();
    }
}

void uart_isr(const void *arg)
{
    if (uart_pending()) {
        handle_uart();
    }
}

void setup(void)
{
    /* Both handlers registered to same IRQ */
    IRQ_CONNECT(SHARED_IRQ, SHARED_PRIO, dma_isr, NULL, 0);
    IRQ_CONNECT(SHARED_IRQ, SHARED_PRIO, uart_isr, NULL, 0);
    irq_enable(SHARED_IRQ);
}
```

#### Dynamic Sharing (Runtime)

```c
void add_handler(unsigned int irq)
{
    irq_connect_dynamic(irq, 2, my_isr, my_arg, 0);
    /* Automatically becomes shared if ISR already exists */
}

void remove_handler(unsigned int irq)
{
    /* Requires CONFIG_DYNAMIC_INTERRUPTS + CONFIG_SHARED_INTERRUPTS */
    irq_disconnect_dynamic(irq, 2, my_isr, my_arg, 0);
}
```

#### ISR Design for Shared Interrupts

Each ISR **must** check if its device caused the interrupt:

```c
void device_a_isr(const void *arg)
{
    struct device_a *dev = (struct device_a *)arg;

    /* Check interrupt source */
    if (!(dev->regs->status & IRQ_PENDING_FLAG)) {
        return;  /* Not our interrupt */
    }

    /* Handle our interrupt */
    dev->regs->status = IRQ_PENDING_FLAG;  /* Clear */
    handle_device_a_event(dev);
}
```

### Multi-Level Interrupts

Nested interrupt controllers (cascaded interrupts).

#### When to Use

- SoC has more interrupt sources than CPU supports natively
- External interrupt controller chips
- Complex SoC designs

#### Configuration

```conf
CONFIG_MULTI_LEVEL_INTERRUPTS=y
CONFIG_2ND_LEVEL_INTERRUPTS=y   # Enable second level
CONFIG_3RD_LEVEL_INTERRUPTS=y   # Enable third level (if needed)

# Bit allocation (must sum to <= 32)
CONFIG_1ST_LEVEL_INTERRUPT_BITS=8
CONFIG_2ND_LEVEL_INTERRUPT_BITS=8
CONFIG_3RD_LEVEL_INTERRUPT_BITS=8
```

#### IRQ Number Encoding

```
                 Level 3    Level 2    Level 1
                 ┌──────┐   ┌──────┐   ┌──────┐
IRQ number:      │ 8 bits│   │ 8 bits│   │ 8 bits│
                 └──────┘   └──────┘   └──────┘
```

Example from Zephyr docs:
```
Device A on Level 1, line 4:     0x00000004
Device B on Level 2, line 2, parent Level 1 line 9:  0x00000302
Device D on Level 3, line 2, parent L2 line 5, parent L1 line 9:  0x00030609
```

#### Usage

Multi-level IRQ numbers work transparently with standard APIs:

```c
/* IRQ number includes level encoding */
#define NESTED_DEVICE_IRQ  0x00000302  /* From devicetree */

void setup(void)
{
    IRQ_CONNECT(NESTED_DEVICE_IRQ, 2, my_isr, NULL, 0);
    irq_enable(NESTED_DEVICE_IRQ);
}
```

### IRQ Locking Patterns

#### Basic Critical Section

```c
void safe_operation(void)
{
    unsigned int key = irq_lock();
    /* Critical section - no interrupts */
    shared_data++;
    irq_unlock(key);
}
```

#### Nested Locking

```c
void outer_function(void)
{
    unsigned int key1 = irq_lock();
    /* Interrupts disabled */

    inner_function();  /* May also lock */

    irq_unlock(key1);
}

void inner_function(void)
{
    unsigned int key2 = irq_lock();  /* Nesting OK */
    /* Still disabled */
    irq_unlock(key2);  /* Restores to key1 state (still disabled) */
}
```

#### IRQ Lock vs Disabling Specific IRQ

```c
/* Lock ALL interrupts (thread-specific) */
unsigned int key = irq_lock();
/* ... */
irq_unlock(key);

/* Disable ONE specific IRQ (system-wide) */
irq_disable(DEVICE_IRQ);
/* ... */
irq_enable(DEVICE_IRQ);
```

**Key difference:**
- `irq_lock()` is thread-specific; released on context switch
- `irq_disable()` is system-wide; persists across threads

#### Thread Context Awareness

```c
void my_function(void)
{
    if (k_is_in_isr()) {
        /* ISR context - cannot hold lock across return */
        /* Use K_NO_WAIT for any blocking operations */
    } else {
        /* Thread context - can use irq_lock() */
        unsigned int key = irq_lock();
        critical_operation();
        irq_unlock(key);
    }
}
```

#### Preventing Preemption

IRQ lock inhibits preemption **only while held and thread does not sleep**:

```c
void atomic_sequence(void)
{
    unsigned int key = irq_lock();

    /* Thread A holds lock here */
    /* If higher-priority thread B becomes ready,
     * it won't run until after irq_unlock() */

    do_step_1();  /* Must NOT sleep/block! */
    do_step_2();
    do_step_3();

    irq_unlock(key);
    /* Thread B may now preempt if ready */
}
```

**Critical:** If a thread holding an IRQ lock **sleeps or blocks**, the lock is released when swapped out. The next thread runs with interrupts enabled. When the original thread resumes, its IRQ lock is automatically re-established.

#### IRQ Lock vs Scheduler Lock

| Mechanism | Prevents Preemption | Prevents Interrupts | Survives Sleep |
|-----------|---------------------|---------------------|----------------|
| `irq_lock()` | Yes (side effect) | Yes | No |
| `k_sched_lock()` | Yes | No | Yes (maintained) |

Use `k_sched_lock()` when you need to prevent preemption but still want interrupts to fire. Use `irq_lock()` when you need atomic access to data shared with ISRs.

### RAM-Based Execution

Relocate ISRs to RAM to avoid flash access latency.

#### Configuration

```conf
CONFIG_SRAM_VECTOR_TABLE=y    # Vector table in RAM
CONFIG_SRAM_SW_ISR_TABLE=y    # SW ISR table in RAM
```

#### Code Relocation

Use Zephyr's code relocation feature to place ISR code in RAM:

```c
/* In CMakeLists.txt or using attributes */
__ramfunc void fast_isr(const void *arg)
{
    /* This function runs from RAM */
}
```

Or via linker script / CMake configuration:

```cmake
# Relocate entire file to RAM
zephyr_code_relocate(FILES src/fast_handlers.c LOCATION SRAM)
```

#### Trade-offs

| Benefit | Cost |
|---------|------|
| Faster ISR execution | More RAM usage |
| Consistent latency | Less flash for code |
| No flash wait states | Startup copy time |

## Api

### Table of Contents

1. [Registration Macros](#registration-macros)
2. [Direct ISR Macros](#direct-isr-macros)
3. [IRQ Control Functions](#irq-control-functions)
4. [Interrupt Locking](#interrupt-locking)
5. [Context Detection](#context-detection)
6. [Kconfig Options](#kconfig-options)
7. [Header Files](#header-files)

### Registration Macros

#### IRQ_CONNECT

Register an ISR at build time.

```c
IRQ_CONNECT(irq_p, priority_p, isr_p, isr_param_p, flags_p)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `irq_p` | `unsigned int` | IRQ line number (must be compile-time constant) |
| `priority_p` | `unsigned int` | Interrupt priority (arch-specific) |
| `isr_p` | `void (*)(const void *)` | ISR function pointer |
| `isr_param_p` | `const void *` | Argument passed to ISR |
| `flags_p` | `uint32_t` | Architecture-specific flags |

**Constraints:**
- All arguments **must** be compile-time constants
- Does not enable the interrupt; call `irq_enable()` after

**Example:**
```c
#define MY_IRQ  24
#define MY_PRIO 2

void my_isr(const void *arg) { /* handler */ }

void setup(void)
{
    IRQ_CONNECT(MY_IRQ, MY_PRIO, my_isr, NULL, 0);
    irq_enable(MY_IRQ);
}
```

#### irq_connect_dynamic

Register an ISR at runtime.

```c
int irq_connect_dynamic(unsigned int irq,
                        unsigned int priority,
                        void (*routine)(const void *parameter),
                        const void *parameter,
                        uint32_t flags);
```

**Returns:** The interrupt vector assigned, or negative error code.

**Requires:** `CONFIG_DYNAMIC_INTERRUPTS=y`

**Example:**
```c
void my_isr(const void *arg) { /* handler */ }

void setup(unsigned int runtime_irq)
{
    int vec = irq_connect_dynamic(runtime_irq, 2, my_isr, NULL, 0);
    if (vec < 0) {
        /* Error handling */
    }
    irq_enable(runtime_irq);
}
```

#### irq_disconnect_dynamic

Disconnect a dynamically registered ISR.

```c
int irq_disconnect_dynamic(unsigned int irq,
                           unsigned int priority,
                           void (*routine)(const void *parameter),
                           const void *parameter,
                           uint32_t flags);
```

**Returns:** 0 on success, negative error code on failure.

**Requires:** `CONFIG_DYNAMIC_INTERRUPTS=y` and `CONFIG_SHARED_INTERRUPTS=y`

### Direct ISR Macros

#### IRQ_DIRECT_CONNECT

Register a direct ISR at build time (lowest latency).

```c
IRQ_DIRECT_CONNECT(irq_p, priority_p, isr_p, flags_p)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `irq_p` | `unsigned int` | IRQ line number |
| `priority_p` | `unsigned int` | Interrupt priority |
| `isr_p` | function | Direct ISR (declared with `ISR_DIRECT_DECLARE`) |
| `flags_p` | `uint32_t` | Flags (e.g., `IRQ_ZERO_LATENCY`) |

**Differences from regular ISR:**
- No argument passed to ISR
- No automatic stack switch (unless HW does it)
- No automatic PM idle exit
- Scheduling decision controlled by return value

#### ISR_DIRECT_DECLARE

Declare a direct ISR handler.

```c
ISR_DIRECT_DECLARE(name)
{
    /* ISR body */
    ISR_DIRECT_PM();  /* Optional: PM idle exit */
    return 1;         /* 1 = reschedule check, 0 = skip */
}
```

**Return value:**
- `0` - Skip scheduling decision (use for zero-latency)
- `1` - Check if rescheduling is needed

#### ISR_DIRECT_HEADER / ISR_DIRECT_FOOTER

Architecture-specific setup/teardown (used internally by `ISR_DIRECT_DECLARE`).

```c
#define ISR_DIRECT_HEADER()            ARCH_ISR_DIRECT_HEADER()
#define ISR_DIRECT_FOOTER(check_resched) ARCH_ISR_DIRECT_FOOTER(check_resched)
```

#### ISR_DIRECT_PM

Power management idle exit hook.

```c
ISR_DIRECT_PM()
```

**Warning:** Must NOT be used in zero-latency ISRs.

### IRQ Control Functions

#### irq_enable

Enable an IRQ line.

```c
#define irq_enable(irq) arch_irq_enable(irq)
```

#### irq_disable

Disable an IRQ line.

```c
#define irq_disable(irq) arch_irq_disable(irq)
```

#### irq_is_enabled

Check if an IRQ is enabled.

```c
#define irq_is_enabled(irq) arch_irq_is_enabled(irq)
```

**Returns:** `true` if enabled, `false` otherwise.

### Interrupt Locking

#### irq_lock

Lock all interrupts (disable globally).

```c
unsigned int irq_lock(void);
```

**Returns:** Lock-out key (opaque value).

**Notes:**
- Can be called recursively
- Each `irq_lock()` must be matched with `irq_unlock()`
- IRQ lock is thread-specific (released on context switch)
- Holding IRQ lock during context switch is illegal

**Example:**
```c
unsigned int key = irq_lock();
/* Critical section - no interrupts */
irq_unlock(key);
```

#### irq_unlock

Unlock interrupts (restore previous state).

```c
void irq_unlock(unsigned int key);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `unsigned int` | Lock-out key from `irq_lock()` |

**Example (nested locking):**
```c
unsigned int key1 = irq_lock();
/* ... */
unsigned int key2 = irq_lock();  /* Nested */
/* ... */
irq_unlock(key2);  /* Restore to key1 state */
/* ... */
irq_unlock(key1);  /* Fully unlocked */
```

### Context Detection

#### k_is_in_isr

Detect if currently executing in ISR context.

```c
bool k_is_in_isr(void);
```

**Returns:** `true` if in ISR context, `false` if in thread context.

**Example:**
```c
void my_function(void)
{
    if (k_is_in_isr()) {
        /* Non-blocking path */
        k_sem_give(&sem);
    } else {
        /* Can block */
        k_sem_take(&sem, K_FOREVER);
    }
}
```

### Kconfig Options

| Option | Description | Default |
|--------|-------------|---------|
| `CONFIG_ISR_STACK_SIZE` | Interrupt stack size in bytes | Arch-dependent |
| `CONFIG_DYNAMIC_INTERRUPTS` | Enable runtime ISR registration | n |
| `CONFIG_SHARED_INTERRUPTS` | Allow multiple ISRs per IRQ line | n |
| `CONFIG_SHARED_IRQ_MAX_NUM_CLIENTS` | Max ISRs per shared IRQ | 2 |
| `CONFIG_ZERO_LATENCY_IRQS` | Enable zero-latency interrupts | n |
| `CONFIG_GEN_IRQ_VECTOR_TABLE` | Generate interrupt vector table | y |
| `CONFIG_MULTI_LEVEL_INTERRUPTS` | Enable nested interrupt controllers | n |
| `CONFIG_2ND_LEVEL_INTERRUPTS` | Enable 2nd level interrupts | n |
| `CONFIG_3RD_LEVEL_INTERRUPTS` | Enable 3rd level interrupts | n |
| `CONFIG_SRAM_VECTOR_TABLE` | Place vector table in RAM | n |

### Header Files

| Header | Contents |
|--------|----------|
| `<zephyr/irq.h>` | Main IRQ API (`IRQ_CONNECT`, `irq_lock`, etc.) |
| `<zephyr/kernel.h>` | `k_is_in_isr()` |
| `<zephyr/arch/<arch>/irq.h>` | Architecture-specific IRQ definitions |
| `<zephyr/sw_isr_table.h>` | Software ISR table structures |

## Isr Types

### Table of Contents

1. [Regular ISRs](#regular-isrs)
2. [Direct ISRs](#direct-isrs)
3. [Dynamic ISRs](#dynamic-isrs)
4. [Comparison](#comparison)

### Regular ISRs

Standard interrupt handlers registered at build time. Most common choice.

#### When to Use

- Arguments known at compile time
- Don't need ultra-low latency
- Need to pass data to ISR via argument

#### Implementation

```c
#include <zephyr/irq.h>

#define DEVICE_IRQ    24
#define DEVICE_PRIO   2

struct device_data {
    volatile uint32_t count;
    struct k_sem ready;
};

static struct device_data my_data;

/* ISR handler - receives argument */
void device_isr(const void *arg)
{
    struct device_data *data = (struct device_data *)arg;
    data->count++;
    k_sem_give(&data->ready);
}

/* Registration and setup */
void device_init(void)
{
    k_sem_init(&my_data.ready, 0, 1);

    /* Register at build time - all args must be constants */
    IRQ_CONNECT(DEVICE_IRQ, DEVICE_PRIO, device_isr, &my_data, 0);

    /* Enable the interrupt */
    irq_enable(DEVICE_IRQ);
}
```

#### What Happens Behind the Scenes

1. ISR address placed in vector table (or SW ISR table)
2. When IRQ fires:
   - Kernel saves context
   - Switches to interrupt stack
   - Retrieves ISR and argument from table
   - Calls ISR with argument
   - On return, checks for reschedule
   - Restores context

### Direct ISRs

Minimal-overhead handlers for latency-critical interrupts.

#### When to Use

- Latency is critical (e.g., motor control, high-speed sampling)
- Don't need ISR argument
- Can handle stack/context manually
- Zero-latency interrupts (ARM Cortex-M)

#### Implementation

```c
#include <zephyr/irq.h>

#define FAST_IRQ      24
#define FAST_PRIO     1

/* Direct ISR - no argument, minimal overhead */
ISR_DIRECT_DECLARE(fast_isr)
{
    /* Minimal work here */
    volatile uint32_t *reg = (uint32_t *)0x40001000;
    *reg = 0x01;  /* Acknowledge HW */

    /* Optional: PM idle exit (NOT for zero-latency) */
    ISR_DIRECT_PM();

    /* Return value:
     * 0 = skip reschedule check (fastest, use for ZLI)
     * 1 = check if reschedule needed
     */
    return 1;
}

void fast_device_init(void)
{
    /* Direct registration - no argument parameter */
    IRQ_DIRECT_CONNECT(FAST_IRQ, FAST_PRIO, fast_isr, 0);
    irq_enable(FAST_IRQ);
}
```

#### Direct ISR Limitations

- **No argument passed** - Can't use ISR parameter
- **No automatic stack switch** - Uses interrupted context's stack (unless HW does it)
- **No automatic PM handling** - Must call `ISR_DIRECT_PM()` manually
- **Interrupts may stay locked** - Depends on architecture
- **Return value matters** - Controls scheduling behavior

#### Zero-Latency Direct ISR (ARM Cortex-M)

```c
#include <zephyr/irq.h>

#define ZLI_IRQ       24
#define ZLI_PRIO      0  /* Highest priority */

/* Zero-latency ISR - bypasses irq_lock() */
ISR_DIRECT_DECLARE(zero_latency_isr)
{
    /* Runs even when irq_lock() is held!
     * Must NOT use ANY kernel APIs
     * Must NOT call ISR_DIRECT_PM()
     * Must return 0 (no reschedule)
     */
    volatile uint32_t *reg = (uint32_t *)0x40001000;
    *reg = 0x01;

    return 0;  /* MUST be 0 for ZLI */
}

void zli_init(void)
{
    /* IRQ_ZERO_LATENCY flag enables ZLI */
    IRQ_DIRECT_CONNECT(ZLI_IRQ, ZLI_PRIO, zero_latency_isr, IRQ_ZERO_LATENCY);
    irq_enable(ZLI_IRQ);
}
```

**Zero-Latency Constraints:**
- ARM Cortex-M only
- Requires `CONFIG_ZERO_LATENCY_IRQS=y`
- No kernel API calls allowed
- No `ISR_DIRECT_PM()` allowed
- Must return 0
- Cannot modify kernel-inspected data

### Dynamic ISRs

ISRs registered at runtime when IRQ number or arguments aren't known at build time.

#### When to Use

- IRQ number determined at runtime (e.g., from devicetree)
- Multiple device instances sharing ISR code
- Plug-and-play devices
- Need to disconnect/reconnect ISRs

#### Implementation

```c
#include <zephyr/irq.h>

struct dyn_device {
    unsigned int irq;
    void *hw_base;
    volatile uint32_t event_count;
};

/* Shared ISR for multiple instances */
void dyn_device_isr(const void *arg)
{
    struct dyn_device *dev = (struct dyn_device *)arg;
    dev->event_count++;
    /* Clear interrupt at dev->hw_base */
}

int dyn_device_init(struct dyn_device *dev, unsigned int irq, void *base)
{
    dev->irq = irq;
    dev->hw_base = base;
    dev->event_count = 0;

    /* Runtime registration */
    int vec = irq_connect_dynamic(irq, 2, dyn_device_isr, dev, 0);
    if (vec < 0) {
        return vec;  /* Error */
    }

    irq_enable(irq);
    return 0;
}

void dyn_device_shutdown(struct dyn_device *dev)
{
    irq_disable(dev->irq);

    /* Disconnect (requires CONFIG_SHARED_INTERRUPTS) */
    irq_disconnect_dynamic(dev->irq, 2, dyn_device_isr, dev, 0);
}
```

#### Dynamic ISR Requirements

- `CONFIG_DYNAMIC_INTERRUPTS=y`
- For disconnect: `CONFIG_SHARED_INTERRUPTS=y`
- Slight runtime overhead vs static registration

### Comparison

| Feature | Regular | Direct | Dynamic |
|---------|---------|--------|---------|
| Registration | Build-time | Build-time | Runtime |
| ISR Argument | Yes | No | Yes |
| Stack Switch | Automatic | Manual/HW | Automatic |
| PM Handling | Automatic | Manual | Automatic |
| Latency | Normal | Lowest | Normal |
| Zero-Latency Support | No | Yes | No |
| Disconnect Support | No | No | Yes* |
| Use Case | General | Time-critical | Flexible |

*Requires `CONFIG_SHARED_INTERRUPTS`

#### Decision Flowchart

```
┌─────────────────────────────────────────┐
│ Need to handle hardware interrupt?       │
└─────────────────────┬───────────────────┘
                      │
        ┌─────────────▼─────────────┐
        │ IRQ known at compile time? │
        └─────────────┬─────────────┘
                      │
          ┌───────────┴───────────┐
          │                       │
         Yes                      No
          │                       │
          ▼                       ▼
┌─────────────────────┐   ┌─────────────────────┐
│ Need ultra-low      │   │ Use Dynamic ISR     │
│ latency?            │   │ irq_connect_dynamic │
└─────────┬───────────┘   └─────────────────────┘
          │
    ┌─────┴─────┐
    │           │
   Yes          No
    │           │
    ▼           ▼
┌─────────┐  ┌─────────────────────┐
│ Direct  │  │ Use Regular ISR     │
│ ISR     │  │ IRQ_CONNECT         │
└─────────┘  └─────────────────────┘
```

## Offloading

### Table of Contents

1. [Why Offload](#why-offload)
2. [Pattern 1: Semaphore Signaling](#pattern-1-semaphore-signaling)
3. [Pattern 2: Work Queue](#pattern-2-work-queue)
4. [Pattern 3: Message Queue](#pattern-3-message-queue)
5. [Pattern 4: FIFO](#pattern-4-fifo)
6. [Comparison](#comparison)

### Why Offload

ISRs should execute quickly for system responsiveness. Time-consuming processing blocks other interrupts and threads.

**Offload when ISR needs to:**
- Process data (parsing, calculations)
- Access slow peripherals
- Make blocking kernel calls
- Perform memory allocation
- Execute long-running algorithms

**ISR-safe operations (no offload needed):**
- Reading/writing hardware registers
- Acknowledging interrupts
- Simple flag updates
- Non-blocking semaphore give
- Work submission

### Pattern 1: Semaphore Signaling

Simplest pattern: ISR signals, dedicated thread processes.

#### Use When

- Single event type
- Thread can determine what happened
- Order of events doesn't matter (or only latest matters)

#### Implementation

```c
#include <zephyr/kernel.h>
#include <zephyr/irq.h>

K_SEM_DEFINE(data_ready, 0, 1);

volatile uint32_t raw_data;

void my_isr(const void *arg)
{
    /* Quick read from hardware */
    raw_data = *(volatile uint32_t *)0x40001000;

    /* Signal processing thread */
    k_sem_give(&data_ready);
}

void processing_thread(void *p1, void *p2, void *p3)
{
    while (1) {
        /* Block until ISR signals */
        k_sem_take(&data_ready, K_FOREVER);

        /* Safe to do time-consuming work here */
        uint32_t local = raw_data;
        process_data(local);  /* Complex processing */
    }
}

K_THREAD_DEFINE(proc_tid, 1024, processing_thread, NULL, NULL, NULL, 5, 0, 0);

void setup(void)
{
    IRQ_CONNECT(MY_IRQ, 2, my_isr, NULL, 0);
    irq_enable(MY_IRQ);
}
```

#### Caveats

- Binary semaphore (limit=1): If ISR fires twice before thread runs, one signal lost
- Use counting semaphore (higher limit) if every event must be processed
- No data passed through semaphore itself

### Pattern 2: Work Queue

Submit work items to the system work queue or custom queue.

#### Use When

- Don't want to manage a dedicated thread
- Work can be deferred
- Processing is moderate duration
- Order matters (FIFO processing)

#### Implementation: System Work Queue

```c
#include <zephyr/kernel.h>
#include <zephyr/irq.h>

struct sensor_work {
    struct k_work work;
    uint32_t sample;
};

static struct sensor_work sensor_data;

void sensor_work_handler(struct k_work *work)
{
    struct sensor_work *sw = CONTAINER_OF(work, struct sensor_work, work);

    /* Can block here, runs in system workqueue thread */
    process_sensor_sample(sw->sample);
}

void sensor_isr(const void *arg)
{
    /* Quick read */
    sensor_data.sample = read_sensor_hw();

    /* Submit to system workqueue */
    k_work_submit(&sensor_data.work);
}

void setup(void)
{
    k_work_init(&sensor_data.work, sensor_work_handler);

    IRQ_CONNECT(SENSOR_IRQ, 2, sensor_isr, NULL, 0);
    irq_enable(SENSOR_IRQ);
}
```

#### Implementation: Delayable Work

For processing that should happen after a delay:

```c
#include <zephyr/kernel.h>

struct debounce_work {
    struct k_work_delayable dwork;
    bool pressed;
};

static struct debounce_work button_data;

void button_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct debounce_work *data = CONTAINER_OF(dwork, struct debounce_work, dwork);

    if (data->pressed) {
        handle_button_press();
    }
}

void button_isr(const void *arg)
{
    button_data.pressed = read_button_state();

    /* Schedule debounced processing in 50ms */
    k_work_reschedule(&button_data.dwork, K_MSEC(50));
}

void setup(void)
{
    k_work_init_delayable(&button_data.dwork, button_handler);

    IRQ_CONNECT(BUTTON_IRQ, 2, button_isr, NULL, 0);
    irq_enable(BUTTON_IRQ);
}
```

#### Implementation: Custom Work Queue

For priority control or isolation:

```c
#include <zephyr/kernel.h>

#define WORKQ_STACK_SIZE 1024
#define WORKQ_PRIORITY   5

K_THREAD_STACK_DEFINE(my_stack, WORKQ_STACK_SIZE);
static struct k_work_q my_workq;

static struct k_work my_work;

void work_handler(struct k_work *work)
{
    /* Processing in dedicated thread */
}

void my_isr(const void *arg)
{
    /* Submit to custom queue (not system queue) */
    k_work_submit_to_queue(&my_workq, &my_work);
}

void setup(void)
{
    /* Create custom work queue with high priority */
    k_work_queue_init(&my_workq);
    k_work_queue_start(&my_workq, my_stack, WORKQ_STACK_SIZE,
                       WORKQ_PRIORITY, NULL);

    k_work_init(&my_work, work_handler);

    IRQ_CONNECT(MY_IRQ, 2, my_isr, NULL, 0);
    irq_enable(MY_IRQ);
}
```

### Pattern 3: Message Queue

Pass structured data from ISR to thread.

#### Use When

- Need to pass data with each event
- Multiple event types
- Must preserve event order
- Can tolerate message loss if queue full

#### Implementation

```c
#include <zephyr/kernel.h>
#include <zephyr/irq.h>

struct sensor_msg {
    uint32_t timestamp;
    uint16_t channel;
    uint16_t value;
};

K_MSGQ_DEFINE(sensor_msgq, sizeof(struct sensor_msg), 10, 4);

void sensor_isr(const void *arg)
{
    struct sensor_msg msg = {
        .timestamp = k_uptime_get_32(),
        .channel = read_channel(),
        .value = read_value()
    };

    /* Non-blocking put - drops message if queue full */
    k_msgq_put(&sensor_msgq, &msg, K_NO_WAIT);
}

void processing_thread(void *p1, void *p2, void *p3)
{
    struct sensor_msg msg;

    while (1) {
        /* Block until message available */
        if (k_msgq_get(&sensor_msgq, &msg, K_FOREVER) == 0) {
            printf("Ch %d: %d @ %u\n",
                   msg.channel, msg.value, msg.timestamp);
            process_measurement(&msg);
        }
    }
}
```

#### Handling Queue Full

```c
void sensor_isr(const void *arg)
{
    struct sensor_msg msg = { /* ... */ };

    if (k_msgq_put(&sensor_msgq, &msg, K_NO_WAIT) != 0) {
        /* Queue full - options:
         * 1. Drop message (shown above)
         * 2. Increment overflow counter
         * 3. Purge oldest and retry
         */
        overflow_count++;
    }
}
```

### Pattern 4: FIFO

Linked-list queue for variable-size data or memory-efficient operation.

#### Use When

- Need to pass varying amounts of data
- Have pre-allocated buffers
- Want zero-copy transfer

#### Implementation

```c
#include <zephyr/kernel.h>
#include <zephyr/irq.h>

struct packet {
    void *fifo_reserved;  /* Required first member */
    uint8_t data[64];
    size_t len;
};

K_FIFO_DEFINE(rx_fifo);

/* Pre-allocated packet pool */
static struct packet packet_pool[4];
K_SEM_DEFINE(packet_sem, 4, 4);

struct packet *packet_alloc(void)
{
    static int idx = 0;
    if (k_sem_take(&packet_sem, K_NO_WAIT) == 0) {
        return &packet_pool[idx++ % 4];
    }
    return NULL;
}

void packet_free(struct packet *pkt)
{
    k_sem_give(&packet_sem);
}

void rx_isr(const void *arg)
{
    struct packet *pkt = packet_alloc();
    if (pkt) {
        pkt->len = read_hw_buffer(pkt->data, sizeof(pkt->data));
        k_fifo_put(&rx_fifo, pkt);
    }
    /* If alloc fails, packet is dropped */
}

void processing_thread(void *p1, void *p2, void *p3)
{
    while (1) {
        struct packet *pkt = k_fifo_get(&rx_fifo, K_FOREVER);

        process_packet(pkt->data, pkt->len);

        packet_free(pkt);
    }
}
```

### Comparison

| Pattern | Data Transfer | Blocking | Ordering | Memory | Best For |
|---------|---------------|----------|----------|--------|----------|
| **Semaphore** | External var | Thread waits | No guarantee | Minimal | Simple signaling |
| **Work Queue** | In work struct | System handles | FIFO | Work struct | Deferred processing |
| **Message Queue** | Copy to queue | Thread waits | FIFO | Fixed-size msgs | Structured events |
| **FIFO** | Pointer/link | Thread waits | FIFO | Pre-allocated | Variable data, zero-copy |

#### Quick Selection

```
Need to pass data from ISR?
├── No → Semaphore (simplest)
├── Yes, small fixed-size → Message Queue
├── Yes, large or variable → FIFO with buffer pool
└── Processing can be deferred? → Work Queue
```
