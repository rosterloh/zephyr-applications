# Threads, Scheduling, and Workqueues

## Overview

This skill provides expert knowledge on Zephyr kernel threading: creating and managing threads, understanding the scheduling model, selecting appropriate priorities, and using workqueues for deferred processing.

### Workflow

#### 1. Determine Thread Type Needed

First, identify what kind of execution context is required:

| Need | Solution |
| :--- | :--- |
| Lengthy/complex processing not suitable for ISR | Create a thread |
| Deferred work from ISR or high-priority thread | Use workqueue |
| Background processing with timeout control | Use delayable work item |
| Single-threaded application | Use main thread directly |

#### 2. Select Priority Class

Zephyr has two priority classes. Choose based on preemption requirements:

**Cooperative threads** (negative priority, e.g., -1 to -CONFIG_NUM_COOP_PRIORITIES):
- Never preempted by scheduler until they voluntarily yield
- Best for: device drivers, performance-critical code, short atomic operations
- Use `K_PRIO_COOP(x)` macro where x=0 is highest cooperative priority

**Preemptive threads** (non-negative priority, 0 to CONFIG_NUM_PREEMPT_PRIORITIES-1):
- Can be preempted by higher-priority threads at any reschedule point
- Best for: application logic, time-sensitive processing
- Use `K_PRIO_PREEMPT(x)` macro where x=0 is highest preemptive priority

**Meta-IRQ threads** (optional, highest priority cooperative):
- Enable with `CONFIG_NUM_METAIRQ_PRIORITIES`
- Can preempt even cooperative threads — use only for interrupt bottom-half processing

**Rule of thumb**: Lower numeric value = higher priority. Priority -2 > -1 > 0 > 1.

#### 3. Implementation

Once requirements are clear, implement using the appropriate reference:

**Step 3a:** For thread creation and lifecycle management:
- Read [#thread-lifecycle](#thread-lifecycle) — Static vs dynamic creation, stacks, start/suspend/resume/abort/join

**Step 3b:** For scheduling behavior and priority decisions:
- Read [#scheduling](#scheduling) — Time slicing, yielding, sleeping, scheduler locking

**Step 3c:** For deferred/background processing:
- Read [#workqueues](#workqueues) — System workqueue, custom workqueues, delayable work

**Step 3d:** For API signatures and configuration:
- Read [#api](#api) — Full function signatures, Kconfig options

### Quick Reference

#### Static Thread Definition (compile-time)

```c
#define STACK_SIZE 1024
#define PRIORITY 5

void my_entry(void *p1, void *p2, void *p3) {
    while (1) {
        /* thread work */
        k_msleep(100);
    }
}

K_THREAD_DEFINE(my_thread, STACK_SIZE, my_entry, NULL, NULL, NULL,
                PRIORITY, 0, 0);  /* 0 = start immediately */
```

#### Dynamic Thread Creation (runtime)

```c
K_THREAD_STACK_DEFINE(my_stack, 1024);
struct k_thread my_thread_data;

k_tid_t tid = k_thread_create(&my_thread_data, my_stack,
                              K_THREAD_STACK_SIZEOF(my_stack),
                              my_entry, NULL, NULL, NULL,
                              PRIORITY, 0, K_NO_WAIT);
k_thread_name_set(tid, "my_thread");
```

#### Workqueue Usage

```c
void work_handler(struct k_work *work) {
    /* deferred processing */
}

K_WORK_DEFINE(my_work, work_handler);

/* From ISR or thread: */
k_work_submit(&my_work);  /* submits to system workqueue */
```

### Common Patterns

#### ISR to Thread Communication

ISR signals thread via semaphore, thread does heavy processing:
```c
K_SEM_DEFINE(data_ready, 0, 1);

void my_isr(void *arg) {
    /* quick ISR work */
    k_sem_give(&data_ready);
}

void processing_thread(void *p1, void *p2, void *p3) {
    while (1) {
        k_sem_take(&data_ready, K_FOREVER);
        /* heavy processing */
    }
}
```

#### Thread Ping-Pong (mutual handoff)

```c
K_SEM_DEFINE(sem_a, 1, 1);  /* thread A goes first */
K_SEM_DEFINE(sem_b, 0, 1);

void thread_a_entry(void *p1, void *p2, void *p3) {
    while (1) {
        k_sem_take(&sem_a, K_FOREVER);
        /* A's work */
        k_sem_give(&sem_b);
    }
}

void thread_b_entry(void *p1, void *p2, void *p3) {
    while (1) {
        k_sem_take(&sem_b, K_FOREVER);
        /* B's work */
        k_sem_give(&sem_a);
    }
}
```

### Common Mistakes

| Mistake | Problem | Fix |
| :--- | :--- | :--- |
| Arbitrary stack buffer | Alignment/MPU issues | Use `K_THREAD_STACK_DEFINE` |
| Wrong stack size parameter | Stack overflow/corruption | Use `K_THREAD_STACK_SIZEOF()` |
| Blocking in cooperative thread | Starves other threads | Yield periodically or use preemptive |
| Not releasing resources before exit | Memory/mutex leaks | Clean up before returning from entry |
| Aborting thread with held mutex | Deadlock | Signal thread to exit gracefully |
| Workqueue handler blocks forever | Queue stalls | Use K_NO_WAIT or bounded waits |

### Source Locations

| Description | Path |
| :--- | :--- |
| **Thread Docs** | `<zephyr-ws>/deps/zephyr/doc/kernel/services/threads` |
| **Scheduling Docs** | `<zephyr-ws>/deps/zephyr/doc/kernel/services/scheduling` |
| **Kernel Header** | `<zephyr-ws>/deps/zephyr/include/zephyr/kernel.h` |
| **Synchronization Sample** | `<zephyr-ws>/deps/zephyr/samples/synchronization` |
| **Philosophers Sample** | `<zephyr-ws>/deps/zephyr/samples/philosophers` |

*Note: `<zephyr-ws>` represents the root of the Zephyr workspace.*

## Api

### Table of Contents
- [Thread Creation & Control](#thread-creation--control)
- [Thread Information](#thread-information)
- [Scheduling Control](#scheduling-control)
- [Stack Management](#stack-management)
- [Workqueue API](#workqueue-api)
- [Kconfig Options](#kconfig-options)
- [Header Files](#header-files)

### Thread Creation & Control

#### k_thread_create

```c
k_tid_t k_thread_create(struct k_thread *new_thread,
                        k_thread_stack_t *stack,
                        size_t stack_size,
                        k_thread_entry_t entry,
                        void *p1, void *p2, void *p3,
                        int prio,
                        uint32_t options,
                        k_timeout_t delay);
```

| Parameter | Description |
| :--- | :--- |
| `new_thread` | Uninitialized `struct k_thread` |
| `stack` | Stack area from `K_THREAD_STACK_DEFINE` |
| `stack_size` | Stack size via `K_THREAD_STACK_SIZEOF()` |
| `entry` | Entry point function `void entry(void*, void*, void*)` |
| `p1, p2, p3` | Arguments passed to entry function |
| `prio` | Thread priority (negative=cooperative, non-negative=preemptive) |
| `options` | Bitwise OR of `K_ESSENTIAL`, `K_FP_REGS`, `K_USER`, etc. |
| `delay` | Start delay (`K_NO_WAIT`, `K_FOREVER`, or `K_MSEC(n)`) |

**Returns**: `k_tid_t` thread ID

#### K_THREAD_DEFINE

```c
K_THREAD_DEFINE(name, stack_size, entry, p1, p2, p3, prio, options, delay);
```

Statically defines thread, stack, and control block at compile time.

| Parameter | Description |
| :--- | :--- |
| `name` | Thread identifier (creates `k_tid_t name`) |
| `stack_size` | Stack size in bytes |
| `entry` | Entry point function |
| `p1, p2, p3` | Entry function arguments |
| `prio` | Priority |
| `options` | Thread options |
| `delay` | Start delay in **milliseconds** (integer, not `k_timeout_t`) |

#### Thread Lifecycle

```c
void k_thread_start(k_tid_t thread);           /* Start delayed thread */
void k_thread_abort(k_tid_t thread);           /* Abort thread */
int k_thread_join(k_tid_t thread, k_timeout_t timeout);  /* Wait for termination */
void k_thread_suspend(k_tid_t thread);         /* Suspend thread */
void k_thread_resume(k_tid_t thread);          /* Resume suspended thread */
```

**k_thread_join returns**:
- `0`: Thread terminated
- `-EAGAIN`: Timeout expired
- `-EBUSY`: Thread is essential or unjoinable
- `-EDEADLK`: Thread trying to join itself

### Thread Information

```c
k_tid_t k_current_get(void);                           /* Get current thread ID */
const char *k_thread_name_get(k_tid_t thread);         /* Get thread name */
int k_thread_name_set(k_tid_t thread, const char *name); /* Set thread name */
int k_thread_name_copy(k_tid_t thread, char *buf, size_t size); /* Copy name to buffer */
```

#### Custom Data

```c
void k_thread_custom_data_set(void *value);    /* Set current thread's custom data */
void *k_thread_custom_data_get(void);          /* Get current thread's custom data */
```

Requires `CONFIG_THREAD_CUSTOM_DATA=y`.

#### Runtime Statistics

```c
int k_thread_runtime_stats_get(k_tid_t thread, k_thread_runtime_stats_t *stats);
int k_thread_runtime_stats_all_get(k_thread_runtime_stats_t *stats);
```

Requires `CONFIG_THREAD_RUNTIME_STATS=y`.

### Scheduling Control

#### Sleep & Wait

```c
int32_t k_sleep(k_timeout_t timeout);   /* Sleep, returns remaining time if woken */
int32_t k_msleep(int32_t ms);           /* Sleep milliseconds */
int32_t k_usleep(int32_t us);           /* Sleep microseconds */
void k_wakeup(k_tid_t thread);          /* Wake sleeping thread early */
void k_busy_wait(uint32_t usec_to_wait); /* Busy wait (no yield) */
void k_yield(void);                      /* Yield to equal/higher priority */
```

#### Priority

```c
void k_thread_priority_set(k_tid_t thread, int prio);  /* Set priority */
int k_thread_priority_get(k_tid_t thread);             /* Get priority */
```

#### Priority Macros

```c
K_PRIO_COOP(x)     /* Cooperative priority (0=highest coop) */
K_PRIO_PREEMPT(x)  /* Preemptive priority (0=highest preempt) */
K_HIGHEST_THREAD_PRIO   /* Highest possible priority */
K_LOWEST_THREAD_PRIO    /* Lowest possible priority (idle) */
```

#### Scheduler Lock

```c
void k_sched_lock(void);    /* Prevent preemption */
void k_sched_unlock(void);  /* Re-enable preemption */
```

#### Time Slicing

```c
void k_thread_time_slice_set(struct k_thread *th,
                             int32_t slice_ticks,
                             void (*expired)(struct k_thread *th, void *data),
                             void *data);
```

#### SMP / CPU Affinity

```c
int k_thread_cpu_pin(k_tid_t thread, int cpu);         /* Pin to specific CPU */
int k_thread_cpu_mask_clear(k_tid_t thread);           /* Clear CPU mask */
int k_thread_cpu_mask_enable_all(k_tid_t thread);      /* Enable all CPUs */
int k_thread_cpu_mask_enable(k_tid_t thread, int cpu); /* Enable specific CPU */
int k_thread_cpu_mask_disable(k_tid_t thread, int cpu);/* Disable specific CPU */
```

### Stack Management

#### Stack Definition Macros

```c
/* User-mode capable stack */
K_THREAD_STACK_DEFINE(name, size);
K_THREAD_STACK_ARRAY_DEFINE(name, num_stacks, size);
K_THREAD_STACK_SIZEOF(sym);  /* Get usable size */

/* Kernel-only stack (smaller footprint) */
K_KERNEL_STACK_DEFINE(name, size);
K_KERNEL_STACK_ARRAY_DEFINE(name, num_stacks, size);
K_KERNEL_STACK_SIZEOF(sym);  /* Get usable size */
```

#### Dynamic Stack Allocation

```c
k_thread_stack_t *k_thread_stack_alloc(size_t size);
int k_thread_stack_free(k_thread_stack_t *stack);
```

Requires `CONFIG_DYNAMIC_THREAD=y`.

### Workqueue API

#### Work Items

```c
/* Definition */
K_WORK_DEFINE(name, handler);
void k_work_init(struct k_work *work, k_work_handler_t handler);

/* Submission */
int k_work_submit(struct k_work *work);                    /* System workqueue */
int k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work);

/* Status */
int k_work_busy_get(const struct k_work *work);            /* Get busy state flags */
bool k_work_is_pending(const struct k_work *work);         /* Check if pending */

/* Cancellation */
int k_work_cancel(struct k_work *work);                    /* Non-blocking cancel */
bool k_work_cancel_sync(struct k_work *work, struct k_work_sync *sync);

/* Synchronization */
bool k_work_flush(struct k_work *work, struct k_work_sync *sync);
```

#### Delayable Work

```c
/* Definition */
K_WORK_DELAYABLE_DEFINE(name, handler);
void k_work_init_delayable(struct k_work_delayable *dwork, k_work_handler_t handler);

/* Scheduling */
int k_work_schedule(struct k_work_delayable *dwork, k_timeout_t delay);
int k_work_schedule_for_queue(struct k_work_q *queue,
                               struct k_work_delayable *dwork,
                               k_timeout_t delay);
int k_work_reschedule(struct k_work_delayable *dwork, k_timeout_t delay);
int k_work_reschedule_for_queue(struct k_work_q *queue,
                                 struct k_work_delayable *dwork,
                                 k_timeout_t delay);

/* Utilities */
struct k_work_delayable *k_work_delayable_from_work(struct k_work *work);
k_ticks_t k_work_delayable_remaining_get(const struct k_work_delayable *dwork);
bool k_work_delayable_is_pending(const struct k_work_delayable *dwork);

/* Cancellation */
int k_work_cancel_delayable(struct k_work_delayable *dwork);
bool k_work_cancel_delayable_sync(struct k_work_delayable *dwork,
                                   struct k_work_sync *sync);
```

#### Custom Workqueue

```c
void k_work_queue_init(struct k_work_q *queue);
void k_work_queue_start(struct k_work_q *queue,
                        k_thread_stack_t *stack,
                        size_t stack_size,
                        int prio,
                        const struct k_work_queue_config *cfg);

int k_work_queue_drain(struct k_work_q *queue, bool plug);
int k_work_queue_unplug(struct k_work_q *queue);
```

#### Triggered Work

```c
void k_work_poll_init(struct k_work_poll *work, k_work_handler_t handler);
int k_work_poll_submit(struct k_work_poll *work,
                       struct k_poll_event *events,
                       int num_events,
                       k_timeout_t timeout);
int k_work_poll_submit_to_queue(struct k_work_q *queue,
                                struct k_work_poll *work,
                                struct k_poll_event *events,
                                int num_events,
                                k_timeout_t timeout);
int k_work_poll_cancel(struct k_work_poll *work);
```

### Kconfig Options

#### Thread Configuration

| Option | Description | Default |
| :--- | :--- | :--- |
| `CONFIG_NUM_COOP_PRIORITIES` | Number of cooperative priorities | 16 |
| `CONFIG_NUM_PREEMPT_PRIORITIES` | Number of preemptive priorities | 15 |
| `CONFIG_NUM_METAIRQ_PRIORITIES` | Meta-IRQ priority levels | 0 |
| `CONFIG_MAIN_THREAD_PRIORITY` | Main thread priority | 0 |
| `CONFIG_MAIN_STACK_SIZE` | Main thread stack size | 1024 |
| `CONFIG_IDLE_STACK_SIZE` | Idle thread stack size | 256 |

#### Time Slicing

| Option | Description | Default |
| :--- | :--- | :--- |
| `CONFIG_TIMESLICING` | Enable time slicing | y |
| `CONFIG_TIMESLICE_SIZE` | Time slice duration (ms) | 0 |
| `CONFIG_TIMESLICE_PRIORITY` | Priority threshold for slicing | 0 |

#### Scheduler Implementation

| Option | Description |
| :--- | :--- |
| `CONFIG_SCHED_SIMPLE` | Simple linked list (few threads) |
| `CONFIG_SCHED_MULTIQ` | Multi-queue (default) |
| `CONFIG_SCHED_SCALABLE` | Red-black tree (many threads) |
| `CONFIG_WAITQ_SIMPLE` | Simple linked list wait queues |
| `CONFIG_WAITQ_SCALABLE` | Scalable wait queues |

#### Thread Features

| Option | Description | Default |
| :--- | :--- | :--- |
| `CONFIG_THREAD_CUSTOM_DATA` | Per-thread custom data | n |
| `CONFIG_THREAD_MONITOR` | Thread list tracking | n |
| `CONFIG_THREAD_NAME` | Thread naming | n |
| `CONFIG_THREAD_STACK_INFO` | Stack usage tracking | n |
| `CONFIG_THREAD_RUNTIME_STATS` | Runtime statistics | n |
| `CONFIG_THREAD_ANALYZER` | Thread analysis tool | n |
| `CONFIG_USERSPACE` | User mode support | n |

#### Workqueue Configuration

| Option | Description | Default |
| :--- | :--- | :--- |
| `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE` | System workqueue stack | 1024 |
| `CONFIG_SYSTEM_WORKQUEUE_PRIORITY` | System workqueue priority | -1 (coop) |
| `CONFIG_SYSTEM_WORKQUEUE_NO_YIELD` | Disable yield between items | n |

#### Dynamic Threads

| Option | Description | Default |
| :--- | :--- | :--- |
| `CONFIG_DYNAMIC_THREAD` | Enable dynamic thread creation | n |
| `CONFIG_DYNAMIC_THREAD_STACK_SIZE` | Default dynamic stack size | 1024 |
| `CONFIG_DYNAMIC_THREAD_ALLOC` | Stack allocation method | n |

#### SMP

| Option | Description |
| :--- | :--- |
| `CONFIG_SMP` | Symmetric multiprocessing |
| `CONFIG_MP_MAX_NUM_CPUS` | Number of CPUs |
| `CONFIG_SCHED_CPU_MASK` | Per-thread CPU affinity (any scheduler backend since 4.5) |
| `CONFIG_SCHED_CPU_MASK_PIN_ONLY` | Restrict affinity to pinning a thread to one CPU |

Under `CONFIG_SCHED_CPU_MASK_PIN_ONLY`, `k_thread_cpu_mask_clear()`,
`k_thread_cpu_mask_enable_all()` and `k_thread_cpu_mask_disable()` assert as
of 4.5 — use `k_thread_cpu_pin()` instead.

### Header Files

```c
#include <zephyr/kernel.h>           /* All thread/workqueue APIs */
#include <zephyr/sys/atomic.h>       /* Atomic operations */
#include <zephyr/debug/thread_analyzer.h>  /* Thread analyzer */
```

### Common Type Definitions

```c
typedef struct k_thread *k_tid_t;    /* Thread ID */
typedef void (*k_thread_entry_t)(void *p1, void *p2, void *p3);  /* Entry function */
typedef void (*k_work_handler_t)(struct k_work *work);  /* Work handler */
```

### Timeout Values

```c
K_NO_WAIT      /* Don't wait, return immediately */
K_FOREVER      /* Wait indefinitely */
K_MSEC(ms)     /* Milliseconds */
K_USEC(us)     /* Microseconds */
K_SECONDS(s)   /* Seconds */
K_MINUTES(m)   /* Minutes */
K_HOURS(h)     /* Hours */
K_TICKS(t)     /* Raw tick count */
```

### Work Item State Flags

```c
K_WORK_QUEUED     /* In workqueue, waiting to run */
K_WORK_RUNNING    /* Currently executing */
K_WORK_CANCELING  /* Cancel requested while running */
K_WORK_DELAYED    /* Scheduled for future submission */
```

### Thread Options

```c
K_ESSENTIAL       /* Abort triggers system error */
K_FP_REGS         /* Uses floating point */
K_SSE_REGS        /* Uses SSE (x86) */
K_USER            /* User mode thread */
K_INHERIT_PERMS   /* Inherit parent permissions */
```

## Scheduling

### Table of Contents
- [Scheduling Algorithm](#scheduling-algorithm)
- [Priority Classes](#priority-classes)
- [Cooperative Behavior](#cooperative-behavior)
- [Preemptive Behavior](#preemptive-behavior)
- [Time Slicing](#time-slicing)
- [Scheduler Locking](#scheduler-locking)
- [Thread Sleeping](#thread-sleeping)
- [CPU Idling](#cpu-idling)

### Scheduling Algorithm

The kernel's scheduler selects the **highest priority ready thread** to run.

Key rules:
1. Lower numeric priority value = higher priority (priority -2 beats priority 5)
2. Among equal-priority threads, longest-waiting thread runs first
3. ISRs always preempt threads (unless interrupts are masked)
4. Cooperative threads run until they voluntarily yield
5. Preemptive threads can be preempted by higher/equal priority threads

#### Reschedule Points

Scheduler evaluates which thread should run at:
- Thread transitions from running to suspended/waiting (e.g., `k_sem_take`, `k_sleep`)
- Thread transitions to ready (e.g., `k_sem_give`, `k_thread_start`)
- Return from ISR to thread context
- Thread calls `k_yield()`
- Time slice expires (if enabled)

### Priority Classes

#### Priority Value Ranges

| Class | Priority Range | Preemptible By Scheduler |
| :--- | :--- | :--- |
| Meta-IRQ | -CONFIG_NUM_METAIRQ_PRIORITIES to -CONFIG_NUM_COOP_PRIORITIES-1 | Can preempt other threads |
| Cooperative | -CONFIG_NUM_COOP_PRIORITIES to -1 | No (must yield) |
| Preemptive | 0 to CONFIG_NUM_PREEMPT_PRIORITIES-1 | Yes |

Default configs: `CONFIG_NUM_COOP_PRIORITIES=16`, `CONFIG_NUM_PREEMPT_PRIORITIES=15`

#### Priority Macros

```c
/* Cooperative priority: x=0 is highest, x=CONFIG_NUM_COOP_PRIORITIES-1 is lowest */
#define MY_COOP_PRIO K_PRIO_COOP(0)   /* highest cooperative */

/* Preemptive priority: x=0 is highest, x=CONFIG_NUM_PREEMPT_PRIORITIES-1 is lowest */
#define MY_PREEMPT_PRIO K_PRIO_PREEMPT(5)  /* mid-range preemptive */

/* Special values */
K_HIGHEST_THREAD_PRIO    /* most negative, highest priority possible */
K_LOWEST_THREAD_PRIO     /* CONFIG_NUM_PREEMPT_PRIORITIES, used by idle */
K_IDLE_PRIO              /* idle thread priority (lowest) */
```

#### Choosing Priority Class

| Use Case | Recommended Class |
| :--- | :--- |
| Device drivers | Cooperative |
| Performance-critical code | Cooperative |
| Short atomic operations | Cooperative |
| General application threads | Preemptive |
| Time-sensitive processing | Higher preemptive |
| Background tasks | Lower preemptive |
| Interrupt bottom-half | Meta-IRQ (if needed) |

#### Changing Priority at Runtime

```c
k_tid_t tid = k_current_get();

/* Set new priority */
k_thread_priority_set(tid, new_priority);

/* Get current priority */
int prio = k_thread_priority_get(tid);
```

Priority changes take effect immediately. A preemptive thread can become cooperative (and vice versa) by changing its priority.

### Cooperative Behavior

Cooperative threads (negative priority) have exclusive CPU use until they:
- Call `k_yield()`
- Call blocking API (`k_sleep`, `k_sem_take`, etc.)
- Return from entry function
- Are aborted

#### Yielding

```c
void cooperative_thread(void *p1, void *p2, void *p3)
{
    while (1) {
        /* Do work */

        /* Option 1: Yield to equal/higher priority threads */
        k_yield();

        /* Option 2: Sleep (allows ALL threads to run) */
        k_msleep(10);
    }
}
```

**`k_yield()` vs `k_sleep()`**:
- `k_yield()`: Places thread at back of its priority queue, reschedules. If no equal/higher priority ready threads exist, calling thread continues immediately.
- `k_sleep()`: Makes thread unready for specified time. All threads (including lower priority) can run.

#### Mutual Exclusion via Cooperation

Cooperative threads can implement critical sections without locks:

```c
void coop_thread(void *p1, void *p2, void *p3)
{
    /* No other thread can interrupt this section */
    access_shared_resource();
    modify_data();
    /* Still safe, we're cooperative */

    k_yield();  /* Now others can run */
}
```

**Warning**: Only works if ALL threads accessing the resource are cooperative and don't block during access.

### Preemptive Behavior

Preemptive threads (non-negative priority) can be preempted when:
- A higher-priority thread becomes ready
- An equal-priority thread becomes ready (with time slicing)
- An ISR returns and a higher/equal priority thread is ready

#### Preemption Example

```
Time  Thread A (prio 5)    Thread B (prio 3)    Event
───────────────────────────────────────────────────────────
0     Running              Blocked              A is running
1     Running              Blocked              A still running
2     ─preempted─          Ready→Running        B unblocked (higher prio)
3     Ready                Running              B runs
4     Ready                Running              B runs
5     Ready→Running        Blocked              B blocks, A resumes
```

### Time Slicing

When enabled, the scheduler gives equal-priority preemptive threads fair CPU time by preempting them after a time slice.

#### Configuration

```
# Enable time slicing
CONFIG_TIMESLICING=y

# Time slice duration (default: 0 = no time slicing)
CONFIG_TIMESLICE_SIZE=10  # milliseconds

# Only apply time slicing at or below this priority (0=all preemptive)
CONFIG_TIMESLICE_PRIORITY=0
```

#### Per-Thread Time Slice

```c
/* Set custom time slice for a specific thread */
k_thread_time_slice_set(&my_thread,
                        K_MSEC(20),      /* slice duration */
                        slice_expired,    /* callback (or NULL) */
                        NULL);            /* callback arg */

void slice_expired(struct k_thread *thread, void *data)
{
    /* Called when thread's time slice expires */
}
```

#### Time Slice Behavior

1. At end of time slice, scheduler implicitly calls `k_yield()` for the thread
2. If no equal-priority ready threads exist, thread continues
3. Cooperative threads and threads above `CONFIG_TIMESLICE_PRIORITY` are exempt

**Note**: Time slicing does NOT guarantee equal CPU time—it only ensures no thread runs longer than one slice without yielding.

### Scheduler Locking

A preemptive thread can temporarily become unpreemptible:

```c
void critical_operation(void)
{
    /* Lock scheduler - thread becomes effectively cooperative */
    k_sched_lock();

    /* Critical section - cannot be preempted */
    modify_shared_state();

    /* Unlock scheduler - preemption restored */
    k_sched_unlock();
}
```

**Behavior**:
- While locked, thread cannot be preempted by scheduler
- Thread can still block (on semaphore, sleep, etc.)—scheduler switches then
- When thread becomes ready again, lock state is preserved
- ISRs still interrupt (scheduler lock != interrupt lock)

**Use case**: More efficient than changing priority for short critical sections.

### Thread Sleeping

#### Sleep APIs

```c
/* Sleep for specified timeout */
int32_t remaining = k_sleep(K_SECONDS(1));
int32_t remaining = k_sleep(K_MSEC(500));
int32_t remaining = k_sleep(K_FOREVER);  /* sleep until woken */

/* Convenience wrappers */
int32_t remaining = k_msleep(500);   /* milliseconds */
int32_t remaining = k_usleep(1000);  /* microseconds */
```

**Return value**: Time remaining if woken early by `k_wakeup()`, or 0 if full duration elapsed.

#### Wake a Sleeping Thread

```c
k_tid_t tid = /* sleeping thread */;
k_wakeup(tid);  /* no effect if thread not sleeping */
```

#### Busy Waiting

For very short delays where context switch overhead exceeds delay:

```c
/* Busy wait (does NOT yield CPU) */
k_busy_wait(100);  /* microseconds */
```

**Use sparingly**: Wastes CPU cycles, starves other threads.

### CPU Idling

Normally handled by the idle thread. Direct use rarely needed.

```c
/* Simple idle (returns on any interrupt) */
k_cpu_idle();

/* Atomic idle (for race-free event waiting) */
unsigned int key = irq_lock();
if (event_not_ready) {
    k_cpu_atomic_idle(key);  /* atomically unlocks and idles */
} else {
    irq_unlock(key);
}
```

**Warning**: Avoid unless implementing custom power management. Idle thread handles this.

### Earliest Deadline First Scheduling

Optional feature for advanced scheduling:

```
CONFIG_SCHED_DEADLINE=y
```

```c
/* Set thread deadline (absolute time) */
k_thread_deadline_set(tid, deadline_cycle);
```

When enabled, among threads with equal static priority, the one with the earlier deadline runs first.

### SMP Considerations

With symmetric multiprocessing (`CONFIG_SMP=y`):

#### CPU Affinity

```c
/* Pin thread to specific CPU */
k_thread_cpu_pin(tid, cpu_id);

/* Set CPU mask (which CPUs thread can run on) */
k_thread_cpu_mask_clear(tid);
k_thread_cpu_mask_enable(tid, 0);  /* enable CPU 0 */
k_thread_cpu_mask_enable(tid, 1);  /* enable CPU 1 */
```

#### SMP Scheduling

- Each CPU runs independent scheduler
- Threads can migrate between CPUs unless pinned
- Global ready queue vs per-CPU queue depends on config

### Scheduler Implementation Options

Choose ready queue implementation based on workload:

| Option | Best For | Trade-off |
| :--- | :--- | :--- |
| `CONFIG_SCHED_SIMPLE` | < 3 runnable threads | Smallest code, O(n) insert |
| `CONFIG_SCHED_MULTIQ` | General use | Array of lists, O(1), more RAM |
| `CONFIG_SCHED_SCALABLE` | > 20 runnable threads | Red-black tree, O(log n), +2KB code |

Similarly for wait queues:
- `CONFIG_WAITQ_SIMPLE`: Doubly-linked list (few waiters)
- `CONFIG_WAITQ_SCALABLE`: Balanced tree (many waiters)

## Thread Lifecycle

### Table of Contents
- [Thread Properties](#thread-properties)
- [Thread Creation](#thread-creation)
- [Thread States](#thread-states)
- [Thread Control](#thread-control)
- [Thread Stack Management](#thread-stack-management)
- [System Threads](#system-threads)

### Thread Properties

Every Zephyr thread has:

| Property | Description |
| :--- | :--- |
| **Stack area** | Memory region for thread's stack (must use special macros) |
| **Thread control block** | `struct k_thread` instance for kernel bookkeeping |
| **Entry point function** | Function invoked on start, receives up to 3 arguments |
| **Scheduling priority** | Integer determining CPU time allocation |
| **Thread options** | Flags for special kernel treatment (see below) |
| **Start delay** | How long kernel waits before starting thread |
| **Execution mode** | Supervisor (default) or user mode |

#### Thread Options

| Option | Effect |
| :--- | :--- |
| `K_ESSENTIAL` | Thread termination/abort triggers fatal system error |
| `K_FP_REGS` | Kernel saves/restores floating point registers on context switch |
| `K_SSE_REGS` | (x86) Kernel saves/restores SSE registers |
| `K_USER` | Thread runs in user mode with reduced privileges (requires `CONFIG_USERSPACE`) |
| `K_INHERIT_PERMS` | Thread inherits parent's kernel object permissions |

Combine options with bitwise OR: `K_ESSENTIAL | K_FP_REGS`

### Thread Creation

#### Static Definition (Compile-time)

Use `K_THREAD_DEFINE` for threads known at compile time. Stack and control block are auto-defined.

```c
#define STACK_SIZE 1024
#define PRIORITY 5

void my_entry_point(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        /* thread processing */
        k_msleep(100);
    }
}

/* Thread starts immediately (delay=0) */
K_THREAD_DEFINE(my_thread_id, STACK_SIZE,
                my_entry_point, NULL, NULL, NULL,
                PRIORITY, 0, 0);

/* Thread with delayed start (500ms) */
K_THREAD_DEFINE(delayed_thread, STACK_SIZE,
                my_entry_point, NULL, NULL, NULL,
                PRIORITY, K_ESSENTIAL, 500);
```

**Note**: The delay parameter in `K_THREAD_DEFINE` is in milliseconds (integer), not `k_timeout_t`.

#### Dynamic Creation (Runtime)

Use `k_thread_create` when thread parameters are determined at runtime.

```c
K_THREAD_STACK_DEFINE(my_stack_area, 1024);
struct k_thread my_thread_data;

void start_my_thread(void)
{
    k_tid_t tid = k_thread_create(
        &my_thread_data,              /* thread control block */
        my_stack_area,                /* stack area */
        K_THREAD_STACK_SIZEOF(my_stack_area),  /* stack size */
        my_entry_point,               /* entry function */
        NULL, NULL, NULL,             /* p1, p2, p3 arguments */
        5,                            /* priority */
        0,                            /* options */
        K_NO_WAIT                     /* start immediately */
    );

    k_thread_name_set(tid, "my_thread");  /* optional: set name for debugging */
}
```

#### Delayed Start

```c
/* Create but don't start */
k_tid_t tid = k_thread_create(&data, stack, size, entry,
                              NULL, NULL, NULL,
                              PRIORITY, 0, K_FOREVER);

/* Later, start the thread */
k_thread_start(tid);

/* Or create with specific delay */
k_tid_t tid = k_thread_create(&data, stack, size, entry,
                              NULL, NULL, NULL,
                              PRIORITY, 0, K_MSEC(500));
```

#### Dynamic Stack Allocation

```c
void *stack = k_thread_stack_alloc(CONFIG_DYNAMIC_THREAD_STACK_SIZE);
if (stack == NULL) {
    /* allocation failed */
    return;
}

k_tid_t tid = k_thread_create(&thread_data, stack,
                              CONFIG_DYNAMIC_THREAD_STACK_SIZE,
                              entry, NULL, NULL, NULL,
                              PRIORITY, 0, K_NO_WAIT);

/* When thread completes, free the stack */
k_thread_join(tid, K_FOREVER);
k_thread_stack_free(stack);
```

### Thread States

A thread is either **ready** (eligible for execution) or **unready** (cannot execute).

#### Factors Making Thread Unready

| Factor | Description |
| :--- | :--- |
| Not started | Thread created with delay or `K_FOREVER` |
| Waiting on kernel object | e.g., `k_sem_take()`, `k_mutex_lock()` with blocking |
| Waiting for timeout | e.g., `k_sleep()`, `k_msleep()` |
| Suspended | `k_thread_suspend()` called |
| Terminated/Aborted | Thread returned from entry or was aborted |

#### State Transitions

```
                 ┌──────────────┐
                 │   Created    │
                 │  (unready)   │
                 └──────┬───────┘
                        │ k_thread_start() or delay expires
                        ▼
┌───────────┐    ┌──────────────┐
│ Suspended │◄───│    Ready     │
│ (unready) │    │              │
└─────┬─────┘    └──────┬───────┘
      │                 │ selected by scheduler
      │ k_thread_resume │
      └────────────────►▼
                 ┌──────────────┐
                 │   Running    │
                 │              │
                 └──────┬───────┘
                        │ blocks, sleeps, or yields
                        ▼
                 ┌──────────────┐
                 │   Waiting    │
                 │  (unready)   │
                 └──────┬───────┘
                        │ wait satisfied
                        ▼
                 [back to Ready]
```

### Thread Control

#### Suspend and Resume

```c
k_tid_t tid = /* ... */;

/* Suspend thread (can suspend itself or others) */
k_thread_suspend(tid);

/* Resume suspended thread */
k_thread_resume(tid);

/* Suspending already-suspended thread has no additional effect */
```

#### Sleep and Wake

```c
/* Current thread sleeps for specified time */
k_sleep(K_SECONDS(1));      /* timeout type */
k_msleep(1000);             /* milliseconds (convenience) */
k_usleep(1000000);          /* microseconds */

/* Wake another thread early */
k_wakeup(tid);              /* no effect if thread not sleeping */
```

#### Yield

```c
/* Voluntarily give up CPU to equal/higher priority ready threads */
k_yield();
```

#### Termination

A thread **terminates** by returning from its entry function:

```c
void my_entry_point(void *p1, void *p2, void *p3)
{
    /* Release any held resources before returning! */
    k_mutex_unlock(&my_mutex);
    k_free(my_buffer);

    /* Thread terminates */
    return;
}
```

**Critical**: Kernel does NOT auto-release resources (mutexes, allocated memory). Clean up before return.

#### Abort

A thread **aborts** when:
- It triggers a fatal error (null pointer, etc.)
- Another thread calls `k_thread_abort(tid)`

```c
/* Abort another thread (prefer graceful termination when possible) */
k_thread_abort(tid);

/* Abort self */
k_thread_abort(k_current_get());
```

#### Join (Wait for Completion)

```c
/* Block until thread terminates or aborts (or timeout) */
int ret = k_thread_join(tid, K_FOREVER);

/* With timeout */
ret = k_thread_join(tid, K_MSEC(5000));
if (ret == -EAGAIN) {
    /* timeout expired, thread still running */
}
```

**Note**: After `k_thread_join` returns successfully, the thread struct memory can be reused.

### Thread Stack Management

#### Stack Macros

| Macro | Use Case |
| :--- | :--- |
| `K_THREAD_STACK_DEFINE(name, size)` | Threads that may run in user mode |
| `K_KERNEL_STACK_DEFINE(name, size)` | Kernel-only threads (smaller footprint) |
| `K_THREAD_STACK_SIZEOF(stack)` | Get actual usable size for thread creation |
| `K_KERNEL_STACK_SIZEOF(stack)` | Get actual usable size for kernel stack |
| `K_THREAD_STACK_ARRAY_DEFINE(name, n, size)` | Array of n stacks |

#### Stack Size Considerations

- Include space for: local variables, function call frames, ISR preemption
- Use `CONFIG_THREAD_ANALYZER` to measure actual usage
- Start larger, then tune down based on measurements

```c
/* Check stack usage at runtime */
#include <zephyr/debug/thread_analyzer.h>

thread_analyzer_print();  /* prints all thread stack usage */
```

#### Guard Against Overflow

Enable stack overflow detection in Kconfig:
```
CONFIG_THREAD_STACK_INFO=y
CONFIG_THREAD_ANALYZER=y
```

### System Threads

Zephyr automatically creates these threads:

#### Main Thread

- Performs kernel initialization
- Calls application's `main()` function
- Default priority: highest preemptive (0) or lowest cooperative (-1) if no preemptive
- Marked essential during init and `main()` execution
- Can terminate normally after `main()` returns

```c
int main(void)
{
    /* Initialization */

    while (1) {
        /* Main thread can be used for application processing */
        k_msleep(1000);
    }

    /* Or return to terminate main thread (other threads continue) */
    return 0;
}
```

#### Idle Thread

- Runs when no other threads are ready
- Activates power management or executes "do nothing" loop
- Lowest priority in system
- Never terminates (essential thread)

#### System Workqueue Thread

- Created when system workqueue is used
- Processes work items submitted via `k_work_submit()`
- Priority configured via `CONFIG_SYSTEM_WORKQUEUE_PRIORITY`

### Thread Custom Data

Each thread has a 32-bit custom data field (requires `CONFIG_THREAD_CUSTOM_DATA=y`):

```c
/* Set custom data */
k_thread_custom_data_set((void *)my_context);

/* Get custom data */
struct my_context *ctx = k_thread_custom_data_get();
```

**Use case**: Store per-thread context when callback functions don't pass user data.

### User Mode Constraints

When `CONFIG_USERSPACE` is enabled and creating threads from user mode:

- Parent must have permissions on child thread and stack objects
- Child and stack must be uninitialized
- `K_USER` option is required
- `K_ESSENTIAL` option is forbidden
- Child priority must be equal or lower than parent

## Workqueues

### Table of Contents
- [Overview](#overview)
- [System Workqueue](#system-workqueue)
- [Custom Workqueues](#custom-workqueues)
- [Work Items](#work-items)
- [Delayable Work](#delayable-work)
- [Triggered Work](#triggered-work)
- [Best Practices](#best-practices)

### Overview

A **workqueue** is a kernel object with a dedicated thread that processes **work items** in FIFO order. Use workqueues to:

- Offload non-urgent processing from ISRs
- Defer work from high-priority threads
- Avoid creating many single-purpose threads

#### When to Use Workqueues vs Threads

| Scenario | Use |
| :--- | :--- |
| Deferred ISR processing | Workqueue |
| Multiple independent short tasks | Workqueue |
| Long-running continuous processing | Dedicated thread |
| Task needs specific priority | Dedicated thread or custom workqueue |
| Many similar background tasks | Workqueue |

### System Workqueue

Zephyr provides a built-in workqueue for general use. Prefer this over creating custom workqueues.

#### Configuration

```
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=1024
CONFIG_SYSTEM_WORKQUEUE_PRIORITY=10
CONFIG_SYSTEM_WORKQUEUE_NO_YIELD=n  # yield between items (default)
```

#### Basic Usage

```c
#include <zephyr/kernel.h>

void my_work_handler(struct k_work *work)
{
    /* Deferred processing here */
    printk("Work item executed\n");
}

/* Static work item definition */
K_WORK_DEFINE(my_work, my_work_handler);

/* Submit from ISR or thread */
void some_event(void)
{
    k_work_submit(&my_work);  /* submits to system workqueue */
}
```

#### Passing Context to Handler

Use `CONTAINER_OF` to access enclosing structure:

```c
struct my_context {
    struct k_work work;
    int value;
    char data[32];
};

void context_handler(struct k_work *work)
{
    struct my_context *ctx = CONTAINER_OF(work, struct my_context, work);
    /* Access ctx->value, ctx->data */
}

struct my_context ctx;

void init_context(void)
{
    k_work_init(&ctx.work, context_handler);
    ctx.value = 42;
}
```

### Custom Workqueues

Create custom workqueues when:
- System workqueue priority doesn't fit
- Work items might block and stall other system work
- Need isolated processing

```c
#define MY_WQ_STACK_SIZE 1024
#define MY_WQ_PRIORITY 5

K_THREAD_STACK_DEFINE(my_wq_stack, MY_WQ_STACK_SIZE);
struct k_work_q my_work_q;

void init_my_workqueue(void)
{
    k_work_queue_init(&my_work_q);
    k_work_queue_start(&my_work_q, my_wq_stack,
                       K_THREAD_STACK_SIZEOF(my_wq_stack),
                       MY_WQ_PRIORITY, NULL);
}

/* Submit to custom workqueue */
void submit_work(void)
{
    k_work_submit_to_queue(&my_work_q, &my_work);
}
```

#### Workqueue Control

```c
/* Drain queue (block until empty) */
k_work_queue_drain(&my_work_q, false);  /* false = allow new submissions after */

/* Drain and plug (block new submissions) */
k_work_queue_drain(&my_work_q, true);

/* Unplug (allow submissions again) */
k_work_queue_unplug(&my_work_q);
```

### Work Items

#### Work Item States

| State | Meaning |
| :--- | :--- |
| `K_WORK_QUEUED` | Waiting in queue to be processed |
| `K_WORK_RUNNING` | Currently executing on workqueue thread |
| `K_WORK_CANCELING` | Cancel requested, still running |
| `K_WORK_DELAYED` | Scheduled for future submission (delayable) |

#### Checking Work Status

```c
/* Get busy state bitmask */
int busy = k_work_busy_get(&my_work);
if (busy & K_WORK_RUNNING) {
    /* Handler is executing */
}

/* Check if pending (queued, scheduled, or running) */
bool pending = k_work_is_pending(&my_work);
```

#### Cancelling Work

```c
/* Non-blocking cancel attempt */
int ret = k_work_cancel(&my_work);
/* Returns: 0=cancelled, -EALREADY=not pending, -EBUSY=running */

/* Blocking cancel (wait until complete) - thread context only */
struct k_work_sync sync;
bool was_pending = k_work_cancel_sync(&my_work, &sync);
/* Returns true if work was pending and is now complete */
```

#### Flushing Work

```c
/* Block until specific work item completes */
struct k_work_sync sync;
bool was_pending = k_work_flush(&my_work, &sync);
```

#### Resubmitting Work

A work item can be resubmitted from its handler:

```c
void periodic_handler(struct k_work *work)
{
    /* Do processing */

    /* Resubmit for continuous operation */
    k_work_submit(work);  /* Safe: item is no longer queued at this point */
}
```

**Important**: Never modify a pending work item (including reinitialization).

### Delayable Work

Schedule work to execute after a delay.

#### Definition and Scheduling

```c
void delayed_handler(struct k_work *work)
{
    /* Get delayable container */
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);

    /* Or if embedded in struct */
    struct my_struct *ctx = CONTAINER_OF(dwork, struct my_struct, dwork);
}

K_WORK_DELAYABLE_DEFINE(my_delayed_work, delayed_handler);

void schedule_delayed(void)
{
    /* Schedule 500ms from now */
    k_work_schedule(&my_delayed_work, K_MSEC(500));
}
```

#### Schedule vs Reschedule

| Function | Behavior if Already Scheduled |
| :--- | :--- |
| `k_work_schedule()` | No change (keeps original deadline) |
| `k_work_reschedule()` | Replaces deadline with new one |

```c
/* First unprocessed data → schedule */
k_work_schedule(&dwork, K_MSEC(100));  /* runs in 100ms */

/* More data arrives before deadline */
k_work_schedule(&dwork, K_MSEC(100));  /* still runs at original time */

/* Use reschedule to extend deadline on each new data */
k_work_reschedule(&dwork, K_MSEC(100));  /* resets to 100ms from now */
```

#### Immediate Submission

```c
/* Submit immediately (bypass delay) */
k_work_schedule(&dwork, K_NO_WAIT);
k_work_reschedule(&dwork, K_NO_WAIT);
```

#### Delayable Work Status

```c
/* Get remaining time until scheduled submission */
k_ticks_t remaining = k_work_delayable_remaining_get(&dwork);

/* Check if pending (delayed, queued, or running) */
bool pending = k_work_delayable_is_pending(&dwork);

/* Cancel delayable work */
k_work_cancel_delayable(&dwork);          /* non-blocking */
k_work_cancel_delayable_sync(&dwork, &sync);  /* blocking */
```

### Triggered Work

Submit work when poll events occur (alternative to dedicated polling thread).

```c
void triggered_handler(struct k_work *work)
{
    struct k_work_poll *pwork = CONTAINER_OF(work, struct k_work_poll, work);
    /* Process triggered event */
}

struct k_work_poll triggered_work;
struct k_poll_event events[1];

void setup_triggered(void)
{
    k_work_poll_init(&triggered_work, triggered_handler);

    /* Watch a semaphore */
    k_poll_event_init(&events[0], K_POLL_TYPE_SEM_AVAILABLE,
                      K_POLL_MODE_NOTIFY_ONLY, &my_sem);

    /* Submit - will execute when semaphore available */
    k_work_poll_submit(&triggered_work, events, 1, K_FOREVER);
}
```

### Best Practices

#### Avoid Race Conditions

Work handlers share state with threads/ISRs. Use synchronization:

```c
/* BAD: race condition */
void handler(struct k_work *work)
{
    shared_data++;  /* not atomic! */
}

/* GOOD: use atomics for simple flags */
atomic_t shared_counter = ATOMIC_INIT(0);
void handler(struct k_work *work)
{
    atomic_inc(&shared_counter);
}

/* GOOD: use mutex for complex data (but don't block forever!) */
void handler(struct k_work *work)
{
    if (k_mutex_lock(&data_mutex, K_MSEC(100)) == 0) {
        /* modify shared data */
        k_mutex_unlock(&data_mutex);
    } else {
        /* Could not get lock, reschedule */
        k_work_submit(work);
    }
}
```

#### Don't Block Forever

A blocking handler stalls all subsequent work items:

```c
/* BAD: blocks entire workqueue */
void handler(struct k_work *work)
{
    k_sem_take(&some_sem, K_FOREVER);  /* may wait indefinitely */
}

/* GOOD: non-blocking with resubmit */
void handler(struct k_work *work)
{
    if (k_sem_take(&some_sem, K_NO_WAIT) != 0) {
        /* Sem not available, try again later */
        k_work_schedule(k_work_delayable_from_work(work), K_MSEC(10));
        return;
    }
    /* Process with semaphore held */
    k_sem_give(&some_sem);
}
```

#### Check Return Values

```c
/* INCOMPLETE: ignoring return value */
k_work_submit(&work);

/* PROPER: check for failure */
int ret = k_work_submit(&work);
if (ret < 0) {
    if (ret == -EBUSY) {
        /* Work is being cancelled */
    } else if (ret == -EINVAL) {
        /* Queue not accepting work (plugged) */
    }
    /* Handle failure appropriately */
}
```

#### Handler Self-Protection

Always verify conditions in handler, don't trust submission state:

```c
struct device_ctx {
    struct k_work_delayable retry_work;
    bool shutdown;
};

void retry_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct device_ctx *ctx = CONTAINER_OF(dwork, struct device_ctx, retry_work);

    /* Check if we should actually do anything */
    if (ctx->shutdown) {
        return;  /* Device shutting down, don't retry */
    }

    /* Do actual work */
}

void shutdown_device(struct device_ctx *ctx)
{
    ctx->shutdown = true;
    /* Cancel may fail if handler running, but handler will check flag */
    (void)k_work_cancel_delayable(&ctx->retry_work);
}
```

#### System Workqueue Guidelines

| Do | Don't |
| :--- | :--- |
| Keep handlers short | Block indefinitely |
| Use K_NO_WAIT for locks | Call k_sleep() in handler |
| Check return values | Assume submission succeeded |
| Use delayable for retries | Busy-loop in handler |

#### When to Create Custom Workqueue

- Work items may block for extended periods
- Need different priority than system workqueue
- Processing must be isolated from other work
- Need to drain/plug for shutdown sequence
