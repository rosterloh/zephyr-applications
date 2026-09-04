# Synchronization Primitives

## Overview

This skill provides expert knowledge on the four main synchronization primitives in the Zephyr kernel: Semaphores, Mutexes, Events, and Condition Variables. It helps in selecting the right primitive for the task and provides implementation details, common patterns, and pitfall avoidance.

### Workflow

#### 1. Selection Strategy

To choose the correct synchronization primitive, first determine the requirements:

-   **ISR Involvement?** Can an ISR give/take/signal, or only threads?
-   **Ownership?** Must the same thread that locks also unlock?
-   **Priority Inversion?** Is priority inheritance needed?
-   **Signaling Pattern?** One-to-one, one-to-many, many-to-many?
-   **Condition Complexity?** Simple event bits or complex state predicates?

**Step 1:** Read [#comparison](#comparison) for the feature matrix and decision flowchart.

#### 2. Implementation

Once the primitive is selected, use the implementation guide to write the code.

**Step 2:** Read the appropriate reference:

-   **Semaphores**: [#semaphores](#semaphores) — Counting/binary semaphores, ISR signaling, resource limiting.
-   **Mutexes**: [#mutexes](#mutexes) — Mutual exclusion, priority inheritance, reentrant locking.
-   **Events**: [#events](#events) — Bitmask events, many-to-many signaling, wait options.
-   **Condition Variables**: [#condvar](#condvar) — Waiting for complex state changes with a mutex.

#### 3. API & Configuration

For complete API signatures, Kconfig options, and resource locations.

**Step 3:** Read [#api](#api) for:

-   Complete API function signatures for all primitives.
-   Relevant Kconfig options.
-   Header file locations.

#### 4. Troubleshooting

Common issues with synchronization:

-   **Deadlocks**: Multiple mutexes locked in inconsistent order.
-   **Priority Inversion**: Use mutexes (not semaphores) when threads of different priorities share resources.
-   **Spurious Wakeups**: Always use `while()` loops with condition variables.
-   **ISR Blocking**: Never wait/block in ISRs — use `K_NO_WAIT` or check return values.

Refer to the **Common Pitfalls** sections in each reference file for detailed guidance.

### Source Locations

| Description | Path |
| :--- | :--- |
| **Synchronization Docs** | `<zephyr-ws>/deps/zephyr/doc/kernel/services/synchronization` |
| **Kernel Header** | `<zephyr-ws>/deps/zephyr/include/zephyr/kernel.h` |
| **Semaphore Sample** | `<zephyr-ws>/deps/zephyr/samples/synchronization` |
| **Condition Var Sample** | `<zephyr-ws>/deps/zephyr/samples/kernel/condition_variables` |
| **Philosophers Sample** | `<zephyr-ws>/deps/zephyr/samples/philosophers` |

*Note: `<zephyr-ws>` represents the root of the Zephyr workspace.*

## Api

Complete API signatures and configuration options for Zephyr synchronization primitives.

### Table of Contents

1. [Semaphore API](#semaphore-api)
2. [Mutex API](#mutex-api)
3. [Events API](#events-api)
4. [Condition Variable API](#condition-variable-api)
5. [Kconfig Options](#kconfig-options)
6. [Header Files](#header-files)

### Semaphore API

#### Types

```c
struct k_sem;  /* Semaphore object */
```

#### Macros

```c
/* Compile-time definition and initialization */
K_SEM_DEFINE(name, initial_count, count_limit)
```

#### Functions

```c
/* Initialize a semaphore */
int k_sem_init(struct k_sem *sem, unsigned int initial_count, unsigned int limit);

/* Give (increment) a semaphore */
void k_sem_give(struct k_sem *sem);

/* Take (decrement) a semaphore with timeout */
int k_sem_take(struct k_sem *sem, k_timeout_t timeout);
/* Returns: 0 on success, -EBUSY if K_NO_WAIT and unavailable, -EAGAIN on timeout */

/* Reset semaphore count to zero */
void k_sem_reset(struct k_sem *sem);

/* Get current semaphore count */
unsigned int k_sem_count_get(struct k_sem *sem);
```

#### Timeout Values

```c
K_NO_WAIT    /* Return immediately if semaphore unavailable */
K_FOREVER    /* Wait indefinitely */
K_MSEC(ms)   /* Wait for specified milliseconds */
K_USEC(us)   /* Wait for specified microseconds */
K_SECONDS(s) /* Wait for specified seconds */
```

### Mutex API

#### Types

```c
struct k_mutex;  /* Mutex object */
```

#### Macros

```c
/* Compile-time definition and initialization */
K_MUTEX_DEFINE(name)
```

#### Functions

```c
/* Initialize a mutex */
int k_mutex_init(struct k_mutex *mutex);

/* Lock a mutex with timeout */
int k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout);
/* Returns: 0 on success, -EBUSY if K_NO_WAIT and locked, -EAGAIN on timeout */

/* Unlock a mutex (must be called by owner) */
int k_mutex_unlock(struct k_mutex *mutex);
/* Returns: 0 on success, -EPERM if not owner, -EINVAL if not locked */
```

### Events API

#### Types

```c
struct k_event;  /* Event object */
```

#### Macros

```c
/* Compile-time definition and initialization */
K_EVENT_DEFINE(name)
```

#### Functions

```c
/* Initialize an event object */
void k_event_init(struct k_event *event);

/* Set events (overwrite) */
void k_event_set(struct k_event *event, uint32_t events);

/* Post events (bitwise OR) */
void k_event_post(struct k_event *event, uint32_t events);

/* Clear specific events */
void k_event_clear(struct k_event *event, uint32_t events);

/* Test (query) current events without waiting */
uint32_t k_event_test(struct k_event *event, uint32_t events_mask);

/* Wait for ANY of the specified events */
uint32_t k_event_wait(struct k_event *event, uint32_t events,
                      bool reset, k_timeout_t timeout);
/* Returns: Matching events, or 0 on timeout */

/* Wait for ALL of the specified events */
uint32_t k_event_wait_all(struct k_event *event, uint32_t events,
                          bool reset, k_timeout_t timeout);
/* Returns: Matching events (all requested), or 0 on timeout */
```

#### Safe Variants (Atomic Clear on Receipt)

```c
/* Wait for ANY, atomically clear matched events */
uint32_t k_event_wait_safe(struct k_event *event, uint32_t events,
                           bool reset, k_timeout_t timeout);

/* Wait for ALL, atomically clear matched events */
uint32_t k_event_wait_all_safe(struct k_event *event, uint32_t events,
                               bool reset, k_timeout_t timeout);
```

#### Parameters

-   `events`: Bitmask of events to wait for (up to 32 bits).
-   `reset`: If `true`, reset ALL events to 0 before waiting (use with caution in multi-waiter scenarios).
-   `timeout`: How long to wait.

### Condition Variable API

#### Types

```c
struct k_condvar;  /* Condition variable object */
```

#### Macros

```c
/* Compile-time definition and initialization */
K_CONDVAR_DEFINE(name)
```

#### Functions

```c
/* Initialize a condition variable */
int k_condvar_init(struct k_condvar *condvar);

/* Wait on a condition variable (must hold mutex) */
int k_condvar_wait(struct k_condvar *condvar, struct k_mutex *mutex,
                   k_timeout_t timeout);
/*
 * Atomically releases mutex and blocks thread.
 * When signaled, reacquires mutex before returning.
 * Returns: 0 on success, -EAGAIN on timeout
 */

/* Signal one waiting thread */
int k_condvar_signal(struct k_condvar *condvar);
/* Returns: 0 on success */

/* Broadcast to all waiting threads */
int k_condvar_broadcast(struct k_condvar *condvar);
/* Returns: 0 on success */
```

### Kconfig Options

#### Events

```
# Enable event objects (required for k_event_* APIs)
CONFIG_EVENTS=y
```

#### Mutex Priority Inheritance

```
# Limit priority elevation (0 = unlimited, default)
# Higher values allow higher priority elevation
CONFIG_PRIORITY_CEILING=0
```

#### General Kernel Options

```
# Enable preemptive scheduling (affects priority inheritance)
CONFIG_PREEMPT_ENABLED=y

# Enable cooperative scheduling
CONFIG_COOP_ENABLED=y
```

### Header Files

All synchronization primitives are declared in the main kernel header:

```c
#include <zephyr/kernel.h>
```

This single include provides access to:
-   `struct k_sem` and semaphore functions
-   `struct k_mutex` and mutex functions
-   `struct k_event` and event functions
-   `struct k_condvar` and condition variable functions
-   Timeout macros (`K_FOREVER`, `K_NO_WAIT`, `K_MSEC`, etc.)

### Return Value Conventions

| Return Value | Meaning |
| :--- | :--- |
| `0` | Success |
| `-EBUSY` | Resource busy (with `K_NO_WAIT`) |
| `-EAGAIN` | Timeout expired |
| `-EPERM` | Permission denied (e.g., unlock by non-owner) |
| `-EINVAL` | Invalid argument or state |

### ISR Safety Summary

| Function | ISR Safe? |
| :--- | :--- |
| `k_sem_give` | Yes |
| `k_sem_take` (K_NO_WAIT) | Yes |
| `k_sem_take` (with timeout) | No |
| `k_mutex_*` | No |
| `k_event_post` | Yes |
| `k_event_set` | Yes |
| `k_event_test` | Yes |
| `k_event_wait*` | No |
| `k_condvar_*` | No |

## Comparison

### Quick Reference Table

| Feature | Semaphores | Mutexes | Events | Condition Variables |
| :--- | :--- | :--- | :--- | :--- |
| **Primary Purpose** | Counting resources, signaling | Mutual Exclusion (Locking) | Signaling multiple conditions (bitmask) | Waiting for complex state changes |
| **Ownership** | No (Anyone can Give/Take) | Yes (Only owner can Unlock) | No | No (but requires mutex) |
| **ISR Safe?** | Yes (Give always, Take w/ K_NO_WAIT) | No | Yes (Post/Set only, no Wait) | No |
| **Reentrant?** | No | Yes (same owner can lock multiple times) | N/A | N/A |
| **Priority Inheritance** | No | Yes | No | No (but mutex used with it has PI) |
| **Multiple Waiters** | Yes (Highest priority, longest wait first) | Yes (Highest priority, longest wait first) | Yes (All matching waiters released) | Yes (Signal one or Broadcast all) |
| **Data Passing** | No | No | Yes (32-bit bitmask) | No |
| **Kconfig Required** | No (always available) | No (always available) | Yes (`CONFIG_EVENTS=y`) | No (always available) |

### Key API Summary

| Primitive | Key Operations |
| :--- | :--- |
| **Semaphore** | `k_sem_init`, `k_sem_give`, `k_sem_take`, `k_sem_reset`, `k_sem_count_get` |
| **Mutex** | `k_mutex_init`, `k_mutex_lock`, `k_mutex_unlock` |
| **Events** | `k_event_init`, `k_event_post`, `k_event_set`, `k_event_wait`, `k_event_wait_all`, `k_event_wait_safe`, `k_event_wait_all_safe` |
| **Condition Variable** | `k_condvar_init`, `k_condvar_wait`, `k_condvar_signal`, `k_condvar_broadcast` |

### Selection Guide

#### Use Semaphores When

-   **ISR-to-Thread signaling:** ISR needs to wake a thread (binary semaphore).
-   **Resource counting:** Limit concurrent access to N resources (counting semaphore).
-   **Simple thread synchronization:** One thread signals another.
-   **Gate/barrier:** Block threads until initialization completes.

**Avoid when:** You need mutual exclusion between threads of different priorities (use mutex for priority inheritance).

#### Use Mutexes When

-   **Protecting shared data:** Exclusive access to variables, buffers, or data structures.
-   **Protecting hardware:** Only one thread should access a peripheral at a time.
-   **Priority-sensitive locking:** Threads of different priorities share a resource (priority inheritance prevents inversion).
-   **Reentrant locking needed:** Same thread may lock from nested function calls.

**Avoid when:** ISRs are involved (use semaphores) or you just need signaling (use semaphores or events).

#### Use Events When

-   **Multiple conditions (bitmask):** Wait for any/all of several independent conditions.
-   **ISR posting multiple flags:** ISRs signal different event types to a single handler thread.
-   **Broadcast to multiple threads:** All threads waiting on a condition should wake.
-   **State machine with flags:** Track system state as a set of bits.

**Avoid when:** You need counting (semaphores), mutual exclusion (mutex), or complex state predicates (condvar).

#### Use Condition Variables When

-   **Complex condition predicate:** Condition is more than simple flag (e.g., "queue not empty AND not shutdown").
-   **Producer-consumer patterns:** Wait for "data available" or "space available" states.
-   **Thread completion tracking:** Wait for N threads to reach a certain point.
-   **Barrier synchronization:** All threads must reach a point before any proceed.

**Avoid when:** ISRs are involved (use semaphores/events) or condition is a simple bitmask (use events).

### Decision Flowchart

```
Need thread/ISR synchronization?
│
├─ Is an ISR involved?
│  │
│  ├─ ISR needs to signal a thread?
│  │  └─ Semaphore (binary, k_sem_give from ISR)
│  │
│  ├─ ISR needs to set multiple flags?
│  │  └─ Events (k_event_post from ISR)
│  │
│  └─ ISR needs to receive a signal?
│     └─ Semaphore with K_NO_WAIT (polling)
│
├─ Need mutual exclusion (protect shared resource)?
│  │
│  ├─ Threads have different priorities?
│  │  └─ Mutex (priority inheritance)
│  │
│  └─ Simple exclusive access needed?
│     └─ Mutex
│
├─ Need to wait for condition/state change?
│  │
│  ├─ Condition is a bitmask of independent flags?
│  │  └─ Events
│  │
│  ├─ Condition is complex predicate (e.g., count > N)?
│  │  └─ Condition Variable + Mutex
│  │
│  └─ Just need to wait for a signal (no state)?
│     └─ Semaphore (binary)
│
├─ Need to limit concurrent access to N resources?
│  └─ Semaphore (counting, initial = N)
│
└─ Need thread-to-thread ping-pong synchronization?
   └─ Two Semaphores
```

### Common Mistakes

| Mistake | Problem | Solution |
| :--- | :--- | :--- |
| Semaphore for mutual exclusion | No priority inheritance → priority inversion | Use Mutex |
| Mutex in ISR | ISRs cannot block | Use Semaphore (K_NO_WAIT) |
| `if` instead of `while` with condvar | Spurious wakeups cause bugs | Always use `while` loop |
| Events without `CONFIG_EVENTS=y` | Compilation error | Add to prj.conf |
| Signaling condvar without mutex | Race condition | Always hold mutex when signaling |
| Forgetting to unlock mutex | Deadlock | Ensure all paths unlock |
| Multiple mutexes in different order | Deadlock | Lock in consistent order |

### Performance Considerations

| Primitive | Overhead | Best For |
| :--- | :--- | :--- |
| Semaphore | Lowest | Simple signaling, ISR interaction |
| Mutex | Low | Thread-only mutual exclusion |
| Events | Low-Medium | Multi-flag signaling |
| Condition Variable | Medium | Complex state waiting |

For high-frequency operations in performance-critical code, prefer semaphores. For correctness with priority-sensitive threads, prefer mutexes.

## Condvar

A condition variable is a synchronization primitive that enables threads to wait until a particular condition (state) occurs.

### Table of Contents

1. [Concepts](#concepts)
2. [Implementation](#implementation)
3. [Signal vs Broadcast](#signal-vs-broadcast)
4. [Use Patterns](#use-patterns)
5. [Common Pitfalls](#common-pitfalls)

### Concepts

-   **Condition Predicate:** The actual condition is in your code (e.g., `queue_empty == false`). The condvar is the waiting mechanism.
-   **Paired with Mutex:** Always used with a mutex that protects the shared state.
-   **Atomic Release-and-Wait:** `k_condvar_wait()` atomically releases the mutex and puts the thread to sleep.
-   **Wake-and-Reacquire:** When signaled, the thread reacquires the mutex before returning.
-   **Signal vs Broadcast:**
    -   `k_condvar_signal()`: Wake ONE waiting thread.
    -   `k_condvar_broadcast()`: Wake ALL waiting threads.
-   **NOT ISR Safe:** Condition variables require mutex operations; cannot be used from ISRs.

### Implementation

#### Defining (Runtime)

```c
struct k_condvar my_condvar;

void init_function(void)
{
    k_condvar_init(&my_condvar);
}
```

#### Defining (Compile-time)

```c
K_CONDVAR_DEFINE(my_condvar);
K_MUTEX_DEFINE(my_mutex);
```

#### Waiting

**IMPORTANT:** Always use a `while` loop to re-check the condition after waking.

```c
k_mutex_lock(&mutex, K_FOREVER);

/* Wait for condition using WHILE loop (not if!) */
while (!condition_is_true()) {
    /* Atomically: release mutex, sleep, then reacquire mutex when woken */
    k_condvar_wait(&condvar, &mutex, K_FOREVER);
}

/* Condition is now true, mutex is held */
do_work();

k_mutex_unlock(&mutex);
```

#### Waiting with Timeout

```c
k_mutex_lock(&mutex, K_FOREVER);

while (!condition_is_true()) {
    int ret = k_condvar_wait(&condvar, &mutex, K_MSEC(100));
    if (ret == -EAGAIN) {
        /* Timeout expired, condition still not true */
        k_mutex_unlock(&mutex);
        return -ETIMEDOUT;
    }
}

/* Condition is true */
k_mutex_unlock(&mutex);
```

#### Signaling (Wake One)

```c
k_mutex_lock(&mutex, K_FOREVER);

/* Modify the shared state */
make_condition_true();

/* Wake one waiting thread */
k_condvar_signal(&condvar);

k_mutex_unlock(&mutex);
```

#### Broadcasting (Wake All)

```c
k_mutex_lock(&mutex, K_FOREVER);

/* Modify the shared state */
change_shared_state();

/* Wake all waiting threads */
k_condvar_broadcast(&condvar);

k_mutex_unlock(&mutex);
```

### Signal vs Broadcast

| Scenario | Use |
| :--- | :--- |
| One waiter should handle the condition | `k_condvar_signal()` |
| Multiple waiters, but only one can proceed | `k_condvar_signal()` |
| Multiple waiters should all wake and re-evaluate | `k_condvar_broadcast()` |
| State change affects all waiters | `k_condvar_broadcast()` |
| Shutdown/termination notification | `k_condvar_broadcast()` |

**Rule of thumb:** When in doubt, use `broadcast`. It's safer but may wake threads unnecessarily.

### Use Patterns

#### Pattern 1: Producer-Consumer Queue

Classic bounded buffer with wait-for-not-empty and wait-for-not-full.

```c
#define QUEUE_SIZE 10

K_MUTEX_DEFINE(queue_mutex);
K_CONDVAR_DEFINE(queue_not_empty);
K_CONDVAR_DEFINE(queue_not_full);

static int queue[QUEUE_SIZE];
static int head, tail, count;

void producer(int item)
{
    k_mutex_lock(&queue_mutex, K_FOREVER);

    /* Wait while queue is full */
    while (count == QUEUE_SIZE) {
        k_condvar_wait(&queue_not_full, &queue_mutex, K_FOREVER);
    }

    /* Add item to queue */
    queue[tail] = item;
    tail = (tail + 1) % QUEUE_SIZE;
    count++;

    /* Signal that queue is no longer empty */
    k_condvar_signal(&queue_not_empty);

    k_mutex_unlock(&queue_mutex);
}

int consumer(void)
{
    k_mutex_lock(&queue_mutex, K_FOREVER);

    /* Wait while queue is empty */
    while (count == 0) {
        k_condvar_wait(&queue_not_empty, &queue_mutex, K_FOREVER);
    }

    /* Remove item from queue */
    int item = queue[head];
    head = (head + 1) % QUEUE_SIZE;
    count--;

    /* Signal that queue is no longer full */
    k_condvar_signal(&queue_not_full);

    k_mutex_unlock(&queue_mutex);

    return item;
}
```

#### Pattern 2: Thread Join / Completion Tracking

Wait for worker threads to complete.

```c
#define NUM_WORKERS 5

K_MUTEX_DEFINE(completion_mutex);
K_CONDVAR_DEFINE(completion_cv);

static int completed_count;

void worker_thread(void *id)
{
    /* Do work */
    do_work((long)id);

    k_mutex_lock(&completion_mutex, K_FOREVER);
    completed_count++;
    k_condvar_signal(&completion_cv);  /* or broadcast if multiple waiters */
    k_mutex_unlock(&completion_mutex);
}

void wait_for_all_workers(void)
{
    k_mutex_lock(&completion_mutex, K_FOREVER);

    while (completed_count < NUM_WORKERS) {
        k_condvar_wait(&completion_cv, &completion_mutex, K_FOREVER);
    }

    k_mutex_unlock(&completion_mutex);
    printk("All workers completed\n");
}
```

#### Pattern 3: Threshold Notification

Wait until a counter reaches a specific value.

```c
K_MUTEX_DEFINE(count_mutex);
K_CONDVAR_DEFINE(count_threshold_cv);

static int count;
#define COUNT_LIMIT 10

void incrementer(void)
{
    k_mutex_lock(&count_mutex, K_FOREVER);
    count++;

    if (count >= COUNT_LIMIT) {
        k_condvar_signal(&count_threshold_cv);
    }

    k_mutex_unlock(&count_mutex);
}

void wait_for_threshold(void)
{
    k_mutex_lock(&count_mutex, K_FOREVER);

    while (count < COUNT_LIMIT) {
        k_condvar_wait(&count_threshold_cv, &count_mutex, K_FOREVER);
    }

    printk("Threshold reached: count = %d\n", count);
    k_mutex_unlock(&count_mutex);
}
```

#### Pattern 4: Barrier (All Threads Synchronize)

Wait until all threads reach a synchronization point.

```c
#define NUM_THREADS 4

K_MUTEX_DEFINE(barrier_mutex);
K_CONDVAR_DEFINE(barrier_cv);

static int arrived_count;
static int barrier_generation;

void barrier_wait(void)
{
    k_mutex_lock(&barrier_mutex, K_FOREVER);

    int my_generation = barrier_generation;
    arrived_count++;

    if (arrived_count == NUM_THREADS) {
        /* Last thread to arrive */
        arrived_count = 0;
        barrier_generation++;
        k_condvar_broadcast(&barrier_cv);  /* Wake all waiting threads */
    } else {
        /* Wait for others */
        while (my_generation == barrier_generation) {
            k_condvar_wait(&barrier_cv, &barrier_mutex, K_FOREVER);
        }
    }

    k_mutex_unlock(&barrier_mutex);
}
```

### Common Pitfalls

#### 1. Using `if` Instead of `while`

**Problem:** Spurious wakeups or multiple threads competing after broadcast.

```c
/* WRONG: Using if */
if (!condition_is_true()) {
    k_condvar_wait(&cv, &mutex, K_FOREVER);
}
/* Condition may still be false after wakeup! */
```

**Solution:** Always use `while`.

```c
/* CORRECT: Using while */
while (!condition_is_true()) {
    k_condvar_wait(&cv, &mutex, K_FOREVER);
}
/* Condition is guaranteed true here */
```

#### 2. Signaling Without Holding the Mutex

**Problem:** Race condition between state change and signal.

```c
/* WRONG: Not holding mutex while signaling */
change_condition();
k_condvar_signal(&cv);  /* Waiter might miss the signal */
```

**Solution:** Always signal while holding the mutex.

```c
/* CORRECT */
k_mutex_lock(&mutex, K_FOREVER);
change_condition();
k_condvar_signal(&cv);
k_mutex_unlock(&mutex);
```

#### 3. Using Condition Variable from ISR

**Problem:** Condition variables require mutex operations; ISRs cannot block.

```c
void isr_handler(void *arg)
{
    /* FORBIDDEN: Cannot use condvar from ISR */
    k_condvar_signal(&cv);
}
```

**Solution:** Use semaphores or events for ISR-to-thread signaling. Have ISR signal a semaphore, then thread signals condvar if needed.

#### 4. Forgetting to Unlock Mutex

**Problem:** Mutex remains locked, other threads deadlock.

```c
void wait_for_condition(void)
{
    k_mutex_lock(&mutex, K_FOREVER);

    while (!condition) {
        k_condvar_wait(&cv, &mutex, K_FOREVER);
    }

    if (error) {
        return;  /* BUG: mutex never unlocked! */
    }

    k_mutex_unlock(&mutex);
}
```

**Solution:** Ensure all code paths unlock the mutex.

#### 5. Using Signal When Broadcast Is Needed

**Problem:** Only one thread wakes when multiple should evaluate the condition.

```c
/* Multiple consumer threads waiting for data */
while (queue_empty()) {
    k_condvar_wait(&cv, &mutex, K_FOREVER);
}

/* Producer adds multiple items */
add_items_to_queue(10);
k_condvar_signal(&cv);  /* Only wakes ONE consumer! */
```

**Solution:** Use `broadcast` when condition change affects multiple waiters.

#### 6. Condition Variable Is NOT the Condition

**Problem:** Confusing the condvar with the actual condition.

```c
/* WRONG mental model */
/* "Wait for condvar" does not mean "wait for condition" */
```

**Correct understanding:**
-   The **condition** is your boolean expression (e.g., `count >= 10`).
-   The **condvar** is the mechanism to efficiently wait and be woken.
-   Always check the actual condition in a loop.

## Events

An event object is a kernel object that implements traditional events using a 32-bit bitmask for many-to-many signaling.

### Table of Contents

1. [Concepts](#concepts)
2. [Implementation](#implementation)
3. [Wait Variants](#wait-variants)
4. [Use Patterns](#use-patterns)
5. [ISR Usage](#isr-usage)
6. [Common Pitfalls](#common-pitfalls)

### Concepts

-   **Bitmask:** A 32-bit value tracks which events have occurred.
-   **Many-to-Many:** Multiple threads/ISRs can post events, multiple threads can wait.
-   **Post vs Set:**
    -   `k_event_post()`: Bitwise OR with existing events (adds to current state).
    -   `k_event_set()`: Overwrites existing events (replaces current state).
-   **Wait Options:**
    -   Wait for ANY of the requested bits (`k_event_wait`).
    -   Wait for ALL of the requested bits (`k_event_wait_all`).
-   **Safe Variants:** Atomically clear events upon receipt (`k_event_wait_safe`, `k_event_wait_all_safe`).
-   **ISR Safe:** ISRs can post/set events but cannot wait.

### Implementation

#### Defining (Runtime)

```c
struct k_event my_event;

void init_function(void)
{
    k_event_init(&my_event);
}
```

#### Defining (Compile-time)

```c
K_EVENT_DEFINE(my_event);
```

#### Setting (Overwrite)

```c
/* Replace all event bits with 0x001 */
k_event_set(&my_event, 0x001);
```

#### Posting (OR)

```c
/* Add bit 0x020 to existing events */
k_event_post(&my_event, 0x020);

/* Example: post from ISR */
void uart_isr(void *arg)
{
    k_event_post(&my_event, EVENT_UART_RX);
}
```

#### Clearing

```c
/* Clear specific bits */
k_event_set(&my_event, k_event_test(&my_event) & ~BITS_TO_CLEAR);

/* Clear all events */
k_event_set(&my_event, 0);
```

### Wait Variants

#### Wait for ANY (without removal)

Returns when ANY of the specified bits are set. Does not clear the events.

```c
uint32_t events;

/* Wait for any of bits 0-11 */
events = k_event_wait(&my_event, 0xFFF, false, K_MSEC(50));
if (events == 0) {
    /* Timeout - no events received */
} else {
    /* events contains the matched bits */
    if (events & 0x001) { /* Handle event 0 */ }
    if (events & 0x002) { /* Handle event 1 */ }
}
```

#### Wait for ALL (without removal)

Returns only when ALL specified bits are set.

```c
uint32_t events;

/* Wait for bits 0, 5, and 8 (0x121) to ALL be set */
events = k_event_wait_all(&my_event, 0x121, false, K_MSEC(50));
if (events == 0) {
    /* Timeout - not all events received */
} else {
    /* All requested events are set */
}
```

#### Wait for ANY (with atomic removal)

Clears the matched bits atomically upon receipt — prevents race conditions.

```c
uint32_t events;

/* Wait and atomically clear received events */
events = k_event_wait_safe(&my_event, 0xFFF, false, K_MSEC(50));
/* events are now cleared from the event object */
```

#### Wait for ALL (with atomic removal)

Clears all specified bits atomically when all are received.

```c
uint32_t events;

events = k_event_wait_all_safe(&my_event, 0x121, false, K_MSEC(50));
/* All bits (0x121) are cleared if they were all set */
```

#### Parameters

-   `event`: Pointer to event object.
-   `events`: Bitmask of events to wait for.
-   `reset`: If `true`, reset ALL events to 0 before waiting (use with care in multi-waiter scenarios).
-   `timeout`: `K_FOREVER`, `K_NO_WAIT`, or `K_MSEC(n)`.

### Use Patterns

#### Pattern 1: Multiple Interrupt Sources

Signal a thread about multiple hardware events.

```c
#define EVENT_UART_RX  BIT(0)
#define EVENT_UART_TX  BIT(1)
#define EVENT_GPIO     BIT(2)
#define EVENT_TIMER    BIT(3)

K_EVENT_DEFINE(hw_events);

void uart_rx_isr(void *arg) { k_event_post(&hw_events, EVENT_UART_RX); }
void uart_tx_isr(void *arg) { k_event_post(&hw_events, EVENT_UART_TX); }
void gpio_isr(void *arg)    { k_event_post(&hw_events, EVENT_GPIO); }
void timer_isr(void *arg)   { k_event_post(&hw_events, EVENT_TIMER); }

void event_handler_thread(void)
{
    while (1) {
        uint32_t events = k_event_wait_safe(&hw_events, 0xF, false, K_FOREVER);

        if (events & EVENT_UART_RX) { handle_uart_rx(); }
        if (events & EVENT_UART_TX) { handle_uart_tx(); }
        if (events & EVENT_GPIO)    { handle_gpio(); }
        if (events & EVENT_TIMER)   { handle_timer(); }
    }
}
```

#### Pattern 2: State Machine with Multiple Conditions

Wait for a specific combination of conditions.

```c
#define STATE_INITIALIZED BIT(0)
#define STATE_CONFIGURED  BIT(1)
#define STATE_CONNECTED   BIT(2)

K_EVENT_DEFINE(system_state);

void initialization_complete(void)
{
    k_event_post(&system_state, STATE_INITIALIZED);
}

void configuration_loaded(void)
{
    k_event_post(&system_state, STATE_CONFIGURED);
}

void network_connected(void)
{
    k_event_post(&system_state, STATE_CONNECTED);
}

void main_application(void)
{
    /* Wait for system to be fully ready (all three conditions) */
    k_event_wait_all(&system_state,
                     STATE_INITIALIZED | STATE_CONFIGURED | STATE_CONNECTED,
                     false, K_FOREVER);

    /* System is ready, start main loop */
    run_application();
}
```

#### Pattern 3: Broadcast to Multiple Threads

Multiple threads wait for the same event.

```c
K_EVENT_DEFINE(shutdown_event);
#define SHUTDOWN_REQUESTED BIT(0)

void request_shutdown(void)
{
    k_event_post(&shutdown_event, SHUTDOWN_REQUESTED);
}

void worker_thread_1(void)
{
    while (1) {
        /* Check for shutdown, non-blocking */
        if (k_event_wait(&shutdown_event, SHUTDOWN_REQUESTED, false, K_NO_WAIT)) {
            break;  /* Shutdown requested */
        }
        do_work_1();
    }
    cleanup_1();
}

void worker_thread_2(void)
{
    while (1) {
        if (k_event_wait(&shutdown_event, SHUTDOWN_REQUESTED, false, K_NO_WAIT)) {
            break;
        }
        do_work_2();
    }
    cleanup_2();
}
```

#### Pattern 4: Producer-Consumer with Status Flags

```c
#define DATA_AVAILABLE BIT(0)
#define BUFFER_FULL    BIT(1)

K_EVENT_DEFINE(buffer_status);

void producer(void)
{
    while (1) {
        /* Wait for buffer to have space */
        if (k_event_wait(&buffer_status, BUFFER_FULL, false, K_NO_WAIT) == 0) {
            produce_data();
            k_event_post(&buffer_status, DATA_AVAILABLE);
        } else {
            k_sleep(K_MSEC(10));  /* Buffer full, wait */
        }
    }
}

void consumer(void)
{
    while (1) {
        k_event_wait_safe(&buffer_status, DATA_AVAILABLE, false, K_FOREVER);
        consume_data();
        /* Clear BUFFER_FULL if buffer now has space */
        if (buffer_has_space()) {
            uint32_t current = k_event_test(&buffer_status);
            k_event_set(&buffer_status, current & ~BUFFER_FULL);
        }
    }
}
```

### ISR Usage

-   **Posting/Setting from ISR:** Always safe. These operations never block.
-   **Waiting from ISR:** Forbidden. ISRs cannot block.

```c
void my_isr(void *arg)
{
    /* Safe: post from ISR */
    k_event_post(&my_event, EVENT_FLAG);

    /* Safe: set from ISR */
    k_event_set(&my_event, NEW_STATE);

    /* Safe: query events (non-blocking) */
    uint32_t current = k_event_test(&my_event);

    /* FORBIDDEN: waiting in ISR */
    /* k_event_wait(&my_event, 0xFFF, false, K_FOREVER);  // NEVER DO THIS */
}
```

### Common Pitfalls

#### 1. Using `reset=true` with Multiple Waiters

**Problem:** One thread resets events, causing other threads to miss them.

```c
/* Thread A */
k_event_wait(&events, MASK_A, true, K_FOREVER);  /* Resets ALL events */

/* Thread B - may miss events that Thread A reset */
k_event_wait(&events, MASK_B, true, K_FOREVER);
```

**Solution:** Use `reset=false` and clear events explicitly with `_safe` variants.

#### 2. Race Condition: Check-Then-Act

**Problem:** Events change between query and action.

```c
/* Thread */
uint32_t e = k_event_test(&events);
if (e & MY_EVENT) {
    /* Event might be cleared by another thread here! */
    handle_event();
}
```

**Solution:** Use `k_event_wait_safe()` for atomic check-and-clear.

#### 3. Waiting in ISR

**Problem:** ISRs cannot block.

```c
void isr_handler(void *arg)
{
    /* CRASH: Cannot wait in ISR */
    k_event_wait(&events, MASK, false, K_FOREVER);
}
```

**Solution:** Use `k_event_test()` in ISRs for non-blocking queries.

#### 4. Event Loss with Post

**Problem:** Posting the same bit multiple times has no cumulative effect.

```c
k_event_post(&events, BIT(0));  /* Bit 0 set */
k_event_post(&events, BIT(0));  /* Still just bit 0 set - second post is "lost" */
/* Consumer only sees one event */
```

**Solution:** For counting events, use semaphores or message queues instead.

#### 5. Forgetting to Enable Events

**Problem:** Events require Kconfig to be enabled.

```
# prj.conf
CONFIG_EVENTS=y
```

Without this, event APIs are not available.

## Locations

Paths to documentation, source code, headers, and samples for Zephyr synchronization primitives.

*Note: `<zephyr-ws>` represents the root of the Zephyr workspace.*

### Documentation

| Resource | Path |
| :--- | :--- |
| **Synchronization Overview** | `<zephyr-ws>/deps/zephyr/doc/kernel/services/synchronization/` |
| **Semaphores Doc** | `<zephyr-ws>/deps/zephyr/doc/kernel/services/synchronization/semaphores.rst` |
| **Mutexes Doc** | `<zephyr-ws>/deps/zephyr/doc/kernel/services/synchronization/mutexes.rst` |
| **Events Doc** | `<zephyr-ws>/deps/zephyr/doc/kernel/services/synchronization/events.rst` |
| **Condition Variables Doc** | `<zephyr-ws>/deps/zephyr/doc/kernel/services/synchronization/condvar.rst` |

### Header Files

| Resource | Path |
| :--- | :--- |
| **Main Kernel Header** | `<zephyr-ws>/deps/zephyr/include/zephyr/kernel.h` |
| **Kernel Includes Dir** | `<zephyr-ws>/deps/zephyr/include/zephyr/kernel/` |

All synchronization primitives (`k_sem`, `k_mutex`, `k_event`, `k_condvar`) are declared in `kernel.h`.

### Kernel Source

| Resource | Path |
| :--- | :--- |
| **Semaphore Implementation** | `<zephyr-ws>/deps/zephyr/kernel/sem.c` |
| **Mutex Implementation** | `<zephyr-ws>/deps/zephyr/kernel/mutex.c` |
| **Events Implementation** | `<zephyr-ws>/deps/zephyr/kernel/events.c` |
| **Condition Variable Impl** | `<zephyr-ws>/deps/zephyr/kernel/condvar.c` |

### Samples

| Sample | Path | Description |
| :--- | :--- | :--- |
| **Synchronization** | `<zephyr-ws>/deps/zephyr/samples/synchronization/` | Basic semaphore ping-pong between threads |
| **Condition Variables** | `<zephyr-ws>/deps/zephyr/samples/kernel/condition_variables/` | Producer-consumer and simple condvar examples |
| **Philosophers** | `<zephyr-ws>/deps/zephyr/samples/philosophers/` | Classic dining philosophers with mutex/semaphore options |

#### Sample Details

##### Synchronization Sample

Demonstrates thread synchronization using semaphores:

-   Two threads alternately print messages
-   Uses binary semaphores for handoff
-   Shows both static and dynamic thread creation

```
<zephyr-ws>/deps/zephyr/samples/synchronization/
├── CMakeLists.txt
├── prj.conf
├── sample.yaml
├── README.rst
└── src/
    └── main.c
```

##### Condition Variables Samples

Two examples demonstrating condition variables:

**condvar/** - Threshold notification pattern:
-   Multiple incrementer threads
-   One watcher thread waits for count threshold
-   Demonstrates `k_condvar_signal`

**simple/** - Worker completion tracking:
-   Multiple worker threads signal completion
-   Main thread waits for all workers
-   Demonstrates `k_condvar_wait` in a while loop

```
<zephyr-ws>/deps/zephyr/samples/kernel/condition_variables/
├── condvar/
│   └── src/main.c
└── simple/
    └── src/main.c
```

##### Philosophers Sample

Classic dining philosophers problem:

-   Configurable to use different synchronization primitives
-   Demonstrates deadlock avoidance (Dijkstra's solution)
-   Shows mutex vs semaphore trade-offs

```
<zephyr-ws>/deps/zephyr/samples/philosophers/
├── CMakeLists.txt
├── prj.conf
├── sample.yaml
├── README.rst
└── src/
    ├── main.c
    └── phil_obj_abstract.h  # Fork abstraction for different primitives
```

### Tests

| Resource | Path |
| :--- | :--- |
| **Kernel Tests** | `<zephyr-ws>/deps/zephyr/tests/kernel/` |
| **Semaphore Tests** | `<zephyr-ws>/deps/zephyr/tests/kernel/semaphore/` |
| **Mutex Tests** | `<zephyr-ws>/deps/zephyr/tests/kernel/mutex/` |
| **Events Tests** | `<zephyr-ws>/deps/zephyr/tests/kernel/events/` |
| **Condition Var Tests** | `<zephyr-ws>/deps/zephyr/tests/kernel/condvar/` |

## Mutexes

A mutex is a kernel object that implements a traditional reentrant mutex with priority inheritance.

### Table of Contents

1. [Concepts](#concepts)
2. [Implementation](#implementation)
3. [Priority Inheritance](#priority-inheritance)
4. [Reentrant Locking](#reentrant-locking)
5. [Use Patterns](#use-patterns)
6. [Common Pitfalls](#common-pitfalls)

### Concepts

-   **Mutual Exclusion:** Ensures only one thread can access a shared resource at a time.
-   **Ownership:** Has an owning thread. Only the owner can unlock the mutex.
-   **Reentrant:** The owner can lock multiple times (must unlock equal times).
-   **Priority Inheritance:** Temporarily elevates owner's priority if higher-priority thread waits.
-   **NOT ISR Safe:** Mutexes cannot be used by ISRs — use semaphores for ISR signaling.
-   **Wait Queue:** Multiple threads can wait. Highest priority thread that waited longest is woken first.

### Implementation

#### Defining (Runtime)

```c
struct k_mutex my_mutex;

void init_function(void)
{
    k_mutex_init(&my_mutex);
}
```

#### Defining (Compile-time)

```c
K_MUTEX_DEFINE(my_mutex);
```

#### Locking

```c
/* Wait forever for the mutex */
k_mutex_lock(&my_mutex, K_FOREVER);

/* Wait with timeout */
if (k_mutex_lock(&my_mutex, K_MSEC(100)) == 0) {
    /* Acquired successfully */
} else {
    /* Timeout expired, mutex not acquired */
}

/* Non-blocking attempt */
if (k_mutex_lock(&my_mutex, K_NO_WAIT) == 0) {
    /* Acquired */
} else {
    /* Mutex already held by another thread */
}
```

#### Unlocking

```c
/* Only the owning thread can unlock */
k_mutex_unlock(&my_mutex);
```

### Priority Inheritance

Priority inheritance prevents **priority inversion** — a situation where a high-priority thread waits for a low-priority thread that is preempted by medium-priority threads.

#### How It Works

1. Thread L (low priority) locks mutex M.
2. Thread H (high priority) tries to lock M and blocks.
3. Kernel temporarily elevates L's priority to match H's.
4. L runs at high priority until it unlocks M.
5. H acquires M and runs. L's priority is restored.

#### Configuration

```
# Limit how high priority can be raised (0 = unlimited)
CONFIG_PRIORITY_CEILING=0
```

#### Multiple Mutex Limitation

Priority inheritance works optimally with one mutex. With multiple mutexes:

-   Base priority is saved when first mutex is locked.
-   Priority may be elevated multiple times.
-   When unlocking, priority is restored to saved base (not intermediate levels).

**Best Practice:** Lock only one mutex at a time when threads of different priorities share resources.

```c
/* Sub-optimal: multiple mutex locks */
k_mutex_lock(&mutex_a, K_FOREVER);
k_mutex_lock(&mutex_b, K_FOREVER);  /* Priority may be elevated */
/* ... */
k_mutex_unlock(&mutex_b);
k_mutex_unlock(&mutex_a);  /* Priority restored to base, not intermediate */

/* Better: single mutex or nested resources */
k_mutex_lock(&resource_mutex, K_FOREVER);
/* Access both resources */
k_mutex_unlock(&resource_mutex);
```

### Reentrant Locking

The owning thread can lock a mutex it already holds. Each lock increments an internal count; each unlock decrements it. The mutex is released when count reaches zero.

```c
K_MUTEX_DEFINE(resource_mutex);

void outer_function(void)
{
    k_mutex_lock(&resource_mutex, K_FOREVER);  /* count = 1 */
    inner_function();
    k_mutex_unlock(&resource_mutex);           /* count = 0, released */
}

void inner_function(void)
{
    k_mutex_lock(&resource_mutex, K_FOREVER);  /* count = 2 (same owner, OK) */
    /* Access resource */
    k_mutex_unlock(&resource_mutex);           /* count = 1 */
}
```

### Use Patterns

#### Pattern 1: Protecting Shared Data

```c
K_MUTEX_DEFINE(data_mutex);
static int shared_counter;

void increment_counter(void)
{
    k_mutex_lock(&data_mutex, K_FOREVER);
    shared_counter++;
    k_mutex_unlock(&data_mutex);
}

int read_counter(void)
{
    int value;
    k_mutex_lock(&data_mutex, K_FOREVER);
    value = shared_counter;
    k_mutex_unlock(&data_mutex);
    return value;
}
```

#### Pattern 2: Protecting Hardware Access

```c
K_MUTEX_DEFINE(display_mutex);

void display_write(const char *text)
{
    k_mutex_lock(&display_mutex, K_FOREVER);
    /* Only one thread can access display hardware */
    hw_display_send(text);
    k_mutex_unlock(&display_mutex);
}
```

#### Pattern 3: Dining Philosophers (Deadlock Avoidance)

Dijkstra's solution: Always acquire lower-numbered fork first.

```c
K_MUTEX_DEFINE(fork0);
K_MUTEX_DEFINE(fork1);
/* ... */

void philosopher(int id)
{
    struct k_mutex *first, *second;

    /* Always pick up lower-numbered fork first */
    if (id == NUM_PHILOSOPHERS - 1) {
        first = &fork0;
        second = &forks[id];
    } else {
        first = &forks[id];
        second = &forks[id + 1];
    }

    while (1) {
        k_mutex_lock(first, K_FOREVER);
        k_mutex_lock(second, K_FOREVER);
        /* Eat */
        k_mutex_unlock(second);
        k_mutex_unlock(first);
        /* Think */
    }
}
```

#### Pattern 4: Try-Lock Pattern

Non-blocking lock attempt for optional work.

```c
void optional_maintenance(void)
{
    if (k_mutex_lock(&resource_mutex, K_NO_WAIT) == 0) {
        /* Got the lock, do maintenance */
        perform_maintenance();
        k_mutex_unlock(&resource_mutex);
    }
    /* If lock not available, skip maintenance this time */
}
```

### Common Pitfalls

#### 1. Using Mutex in ISR

**Problem:** Mutexes require thread context and cannot be used in ISRs.

```c
void isr_handler(void *arg)
{
    /* CRASH: Cannot use mutex in ISR */
    k_mutex_lock(&my_mutex, K_FOREVER);
}
```

**Solution:** Use semaphores to signal from ISR to thread.

#### 2. Unlocking from Wrong Thread

**Problem:** Only the owner can unlock a mutex.

```c
void thread_a(void)
{
    k_mutex_lock(&mutex, K_FOREVER);
    /* Start thread B to "help" */
}

void thread_b(void)
{
    /* ERROR: Thread B is not the owner */
    k_mutex_unlock(&mutex);  /* Returns error, mutex stays locked */
}
```

**Solution:** Ensure the same thread that locks also unlocks.

#### 3. Deadlock from Inconsistent Lock Order

**Problem:** Two threads lock multiple mutexes in different orders.

```c
/* Thread A */
k_mutex_lock(&mutex1, K_FOREVER);
k_mutex_lock(&mutex2, K_FOREVER);  /* Waits for B */

/* Thread B (simultaneously) */
k_mutex_lock(&mutex2, K_FOREVER);
k_mutex_lock(&mutex1, K_FOREVER);  /* Waits for A */

/* DEADLOCK: Both threads wait forever */
```

**Solution:** Always lock mutexes in the same order across all threads.

#### 4. Forgetting to Unlock

**Problem:** Mutex remains locked, blocking other threads forever.

```c
void process_data(void)
{
    k_mutex_lock(&data_mutex, K_FOREVER);

    if (error_condition) {
        return;  /* BUG: mutex never unlocked! */
    }

    /* ... process ... */
    k_mutex_unlock(&data_mutex);
}
```

**Solution:** Ensure all code paths unlock the mutex.

```c
void process_data(void)
{
    k_mutex_lock(&data_mutex, K_FOREVER);

    if (error_condition) {
        k_mutex_unlock(&data_mutex);
        return;
    }

    /* ... process ... */
    k_mutex_unlock(&data_mutex);
}
```

#### 5. Using Semaphore Instead of Mutex

**Problem:** Semaphores lack ownership and priority inheritance.

```c
/* BAD: Semaphore as lock */
K_SEM_DEFINE(lock, 1, 1);

void low_priority_thread(void)
{
    k_sem_take(&lock, K_FOREVER);
    /* High priority thread waits without priority boost */
    /* ... long operation ... */
    k_sem_give(&lock);
}
```

**Solution:** Use mutex when protecting shared resources between threads.

## Semaphores

A semaphore is a kernel object that implements a traditional counting semaphore.

### Table of Contents

1. [Concepts](#concepts)
2. [Implementation](#implementation)
3. [Use Patterns](#use-patterns)
4. [ISR Usage](#isr-usage)
5. [Common Pitfalls](#common-pitfalls)

### Concepts

-   **Count & Limit:** Has a current count (number of times it can be taken) and a maximum limit.
-   **Give/Take:** `k_sem_give()` increments count (up to limit). `k_sem_take()` decrements count (blocks if 0).
-   **No Ownership:** Any thread or ISR can give. Any thread can take. No tracking of who "owns" the semaphore.
-   **ISR Safe:** ISRs can give. ISRs can take only with `K_NO_WAIT`.
-   **Wait Queue:** Multiple threads can wait on a semaphore. When given, the highest priority thread that waited longest is woken.

### Implementation

#### Defining (Runtime)

```c
struct k_sem my_sem;

void init_function(void)
{
    /* Initialize with initial_count=0, limit=1 (Binary Semaphore) */
    k_sem_init(&my_sem, 0, 1);
}
```

#### Defining (Compile-time)

```c
/* Binary semaphore: initial=0, limit=1 */
K_SEM_DEFINE(my_sem, 0, 1);

/* Counting semaphore: initial=5 available, limit=5 max */
K_SEM_DEFINE(resource_sem, 5, 5);
```

#### Giving (Signaling)

```c
/* Increment count (up to limit). Never blocks. */
k_sem_give(&my_sem);
```

#### Taking (Waiting)

```c
/* Wait forever */
k_sem_take(&my_sem, K_FOREVER);

/* Wait with timeout */
if (k_sem_take(&my_sem, K_MSEC(50)) == 0) {
    /* Acquired successfully */
} else {
    /* Timeout expired, semaphore not acquired */
}

/* Non-blocking (for ISRs or polling) */
if (k_sem_take(&my_sem, K_NO_WAIT) == 0) {
    /* Acquired */
}
```

#### Resetting

```c
/* Reset count to 0, wake no waiting threads */
k_sem_reset(&my_sem);
```

#### Querying Count

```c
unsigned int count = k_sem_count_get(&my_sem);
```

### Use Patterns

#### Pattern 1: Binary Semaphore for Signaling (ISR-to-Thread)

Use when an ISR needs to signal a thread that work is ready.

```c
K_SEM_DEFINE(data_ready_sem, 0, 1);

void isr_handler(void *arg)
{
    /* Data arrived, signal the processing thread */
    k_sem_give(&data_ready_sem);
}

void processing_thread(void)
{
    while (1) {
        /* Block until ISR signals data is ready */
        k_sem_take(&data_ready_sem, K_FOREVER);
        /* Process the data */
        process_incoming_data();
    }
}
```

#### Pattern 2: Counting Semaphore for Resource Pool

Use to limit concurrent access to a pool of resources.

```c
#define NUM_BUFFERS 3
K_SEM_DEFINE(buffer_sem, NUM_BUFFERS, NUM_BUFFERS);

void *acquire_buffer(void)
{
    /* Wait for a buffer to become available */
    k_sem_take(&buffer_sem, K_FOREVER);
    return allocate_from_pool();
}

void release_buffer(void *buf)
{
    return_to_pool(buf);
    k_sem_give(&buffer_sem);
}
```

#### Pattern 3: Thread Ping-Pong Synchronization

Use two semaphores to alternate execution between threads.

```c
K_SEM_DEFINE(sem_a, 1, 1);  /* Thread A starts first */
K_SEM_DEFINE(sem_b, 0, 1);

void thread_a(void)
{
    while (1) {
        k_sem_take(&sem_a, K_FOREVER);
        /* Do work */
        k_sem_give(&sem_b);  /* Hand off to thread B */
    }
}

void thread_b(void)
{
    while (1) {
        k_sem_take(&sem_b, K_FOREVER);
        /* Do work */
        k_sem_give(&sem_a);  /* Hand off to thread A */
    }
}
```

#### Pattern 4: Gate (Barrier)

Block all threads until an initialization completes.

```c
K_SEM_DEFINE(init_gate, 0, 1);

void init_thread(void)
{
    /* Perform initialization */
    do_hardware_init();
    /* Open the gate */
    k_sem_give(&init_gate);
}

void worker_thread(void)
{
    /* Wait for initialization to complete */
    k_sem_take(&init_gate, K_FOREVER);
    /* Give back so other threads can proceed */
    k_sem_give(&init_gate);

    /* Now do work */
    do_work();
}
```

### ISR Usage

-   **Giving from ISR:** Always safe. `k_sem_give()` never blocks.
-   **Taking from ISR:** Only with `K_NO_WAIT`. ISRs must never block.

```c
void my_isr(void *arg)
{
    /* Safe: giving from ISR */
    k_sem_give(&my_sem);

    /* Safe: non-blocking take */
    if (k_sem_take(&another_sem, K_NO_WAIT) == 0) {
        /* Acquired */
    }

    /* FORBIDDEN: blocking take in ISR */
    /* k_sem_take(&my_sem, K_FOREVER);  // NEVER DO THIS */
}
```

### Common Pitfalls

#### 1. Using Semaphores for Mutual Exclusion (Instead of Mutex)

**Problem:** Semaphores lack ownership and priority inheritance.

```c
/* BAD: Using semaphore as a lock between threads of different priorities */
K_SEM_DEFINE(lock_sem, 1, 1);

void low_priority_thread(void) {
    k_sem_take(&lock_sem, K_FOREVER);
    /* ... long operation ... */
    k_sem_give(&lock_sem);  /* High priority thread waits without priority inheritance */
}
```

**Solution:** Use `k_mutex` for mutual exclusion between threads.

#### 2. Giving Multiple Times Without Taking

**Problem:** Giving beyond the limit has no effect; signals can be "lost."

```c
K_SEM_DEFINE(sem, 0, 1);

/* ISR fires twice before thread runs */
k_sem_give(&sem);  /* count = 1 */
k_sem_give(&sem);  /* count still 1 (limit reached) - second signal lost! */

/* Thread only sees one signal */
k_sem_take(&sem, K_FOREVER);  /* count = 0 */
```

**Solution:** Use counting semaphore with higher limit, or use events/message queues.

#### 3. Blocking in ISR

**Problem:** Calling `k_sem_take()` with timeout in ISR causes system crash.

```c
void isr_handler(void *arg)
{
    /* CRASH: Never block in ISR */
    k_sem_take(&sem, K_MSEC(100));
}
```

**Solution:** Always use `K_NO_WAIT` in ISRs and handle the failure case.

#### 4. Forgetting to Initialize

**Problem:** Using `struct k_sem` without initialization causes undefined behavior.

```c
struct k_sem my_sem;
/* WRONG: Using without init */
k_sem_take(&my_sem, K_FOREVER);  /* Undefined behavior */
```

**Solution:** Always call `k_sem_init()` or use `K_SEM_DEFINE()`.
