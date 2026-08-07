---
name: zephyr-kernel
description: >
  Zephyr RTOS kernel primitives: threads, ISRs, scheduling, workqueues,
  synchronization (semaphores, mutexes, events, condvars), inter-thread
  data passing (FIFO/LIFO/queue/mailbox/pipe/ZBus/ring buffer), memory
  management (heap, slab, blocks, domains), state machines (SMF), and
  network buffers (net_buf). Use when creating threads, choosing
  priorities, writing ISR handlers, picking a sync primitive, passing
  data between threads or ISR-to-thread, allocating buffers, or
  implementing a state machine. Triggers on k_thread_*, k_sem_*,
  k_mutex_*, IRQ_CONNECT, k_work_*, k_heap_*, SMF_CREATE_STATE,
  net_buf_alloc, "priority inversion", "deferred work", "ring buffer",
  or any question about what synchronization primitive to use.
---

# Zephyr Kernel

Validated against: Zephyr 4.4.99 (a632b9723bab, 2026-08-07). Re-check with `mise run check-skills`.

## Scope

Core kernel primitives — anything that runs in thread or ISR context
and needs scheduling, synchronization, or inter-context data passing.
Does NOT cover driver-model lifecycle (see `zephyr-peripherals`) or
filesystem/storage subsystems (see `zephyr-system`).

## Pick the right reference

| You're working on...                                              | Load                              |
|-------------------------------------------------------------------|-----------------------------------|
| Threads, priorities, scheduling, workqueues, time slicing         | `references/threading.md`         |
| ISR handlers, IRQ_CONNECT, zero-latency IRQs, ISR offloading      | `references/isr.md`               |
| Semaphores, mutexes, events, condvars, picking a sync primitive   | `references/synchronization.md`   |
| FIFO/LIFO, message queue, mailbox, pipe, ZBus, ring buffer        | `references/data-passing.md`      |
| Heap, memory slab, memory blocks, memory domains, virtual memory  | `references/memory.md`            |
| State machine framework (SMF), HSMs, entry/run/exit actions       | `references/smf.md`               |
| `net_buf` allocation, fragmentation, headroom/tailroom            | `references/netbuf.md`            |

## Universal traps

- **ISRs cannot block.** Never call `k_sleep`, `k_mutex_lock`, or any
  API with a non-`K_NO_WAIT` timeout from ISR context. Use `k_sem_give`
  / a workqueue submit / ZBus publish to defer.
- **Thread stacks are sized in bytes, not words.** Underflow corrupts
  neighbouring memory and usually faults elsewhere — enable
  `CONFIG_STACK_SENTINEL` and `CONFIG_THREAD_ANALYZER` while bringing
  up a new thread.
- **`K_MSEC()`, `K_SECONDS()` are macros, not integers.** Passing a raw
  ms value where a `k_timeout_t` is expected silently mis-converts on
  some architectures.
- **Cooperative threads never get preempted.** A `k_yield()` inside a
  cooperative thread is the only way to let an equal-or-lower priority
  cooperative thread run; preemptive threads of higher priority still
  preempt freely.
- **Priority inversion needs `CONFIG_PRIORITY_CEILING` on
  `k_mutex_t`.** Default mutexes inherit priority but don't ceiling —
  fine for most cases, important to know for hard real-time paths.
