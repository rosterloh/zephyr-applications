# Data Passing Between Threads and ISRs

## Overview

This skill provides expert knowledge on Zephyr kernel objects and subsystems used for data passing. It helps in selecting the right mechanism for the task and provides implementation details.

### Workflow

#### 1. Selection Strategy
To choose the correct data passing mechanism, first determine the requirements:
*   **Participants**: Thread-to-Thread? ISR-to-Thread? One-to-Many?
*   **Data Size**: Fixed small structure? Arbitrary size? Byte stream?
*   **Behavior**: FIFO? LIFO? Synchronous? Pub/Sub?
*   **Coupling**: Tight (direct reference) or loose (decoupled)?

**Step 1:** Read [#comparison](#comparison) to see the features, pros, cons, and a decision flowchart.

#### 2. Implementation
Once the mechanism is selected, use the implementation guide to write the code.

**Step 2:** Read the appropriate reference:
*   **Kernel Objects**: [#data_passing](#data_passing) for FIFO, LIFO, Stack, Message Queue, Mailbox, Pipe.
*   **ZBus (Pub/Sub)**: [#zbus](#zbus) for publish-subscribe communication.
*   **Ring Buffer**: [#ring_buffer](#ring_buffer) for low-level byte stream buffering.

#### 3. API & Configuration
For API signatures, Kconfig options, and sample locations.

**Step 3:** Read [#api](#api) for:
*   Complete API function signatures.
*   Kconfig options for each mechanism.
*   Header file and sample locations.

#### 4. Troubleshooting
If debugging data passing issues, refer to the **Common Pitfalls** section at the end of [#data_passing](#data_passing).

### Source Locations

| Description | Path |
| :--- | :--- |
| **Data Passing Docs** | `<zephyr-ws-dir>/zephyr/doc/kernel/services/data_passing` |
| **ZBus Docs** | `<zephyr-ws-dir>/zephyr/doc/services/zbus/` |
| **Kernel Header** | `<zephyr-ws-dir>/zephyr/include/zephyr/kernel.h` |
| **ZBus Header** | `<zephyr-ws-dir>/zephyr/include/zephyr/zbus/zbus.h` |
| **Ring Buffer Header** | `<zephyr-ws-dir>/zephyr/include/zephyr/sys/ring_buffer.h` |
| **MsgQ Sample** | `<zephyr-ws-dir>/zephyr/samples/kernel/msg_queue` |
| **ZBus Samples** | `<zephyr-ws-dir>/zephyr/samples/subsys/zbus/` |
| **Philosophers Sample** | `<zephyr-ws-dir>/zephyr/samples/philosophers` |

## Api

### Header Files

| Object | Header |
| :--- | :--- |
| FIFO, LIFO, Stack, Message Queue, Mailbox, Pipe | `<zephyr/kernel.h>` |
| ZBus | `<zephyr/zbus/zbus.h>` |
| Ring Buffer | `<zephyr/sys/ring_buffer.h>` |

### Kconfig Options

#### FIFO / LIFO

No specific Kconfig required. Always available.

#### Stack

No specific config, always available.

#### Message Queue

```kconfig
CONFIG_NUM_MBOX_ASYNC_MSGS=10  # Max async mailbox messages (if using async)
```

#### Mailbox

```kconfig
CONFIG_NUM_MBOX_ASYNC_MSGS=10  # Number of async message descriptors
```

#### Pipe

No specific config, always available.

#### ZBus

```kconfig
CONFIG_ZBUS=y                    # Enable ZBus subsystem
CONFIG_ZBUS_CHANNEL_NAME=y       # Include channel names (debugging)
CONFIG_ZBUS_RUNTIME_OBSERVERS=y  # Allow runtime observer add/remove
CONFIG_ZBUS_ASSERT_MOCK=y        # Enable mocking for tests
```

#### Ring Buffer

Part of core, always available.

---

### Key API Functions

#### FIFO

```c
void k_fifo_init(struct k_fifo *fifo);
void k_fifo_put(struct k_fifo *fifo, void *data);
void *k_fifo_get(struct k_fifo *fifo, k_timeout_t timeout);
void *k_fifo_peek_head(struct k_fifo *fifo);
void *k_fifo_peek_tail(struct k_fifo *fifo);
int k_fifo_alloc_put(struct k_fifo *fifo, void *data);  /* Uses heap */
void k_fifo_cancel_wait(struct k_fifo *fifo);
```

**Macros:**
- `K_FIFO_DEFINE(name)` - Static definition

#### LIFO

```c
void k_lifo_init(struct k_lifo *lifo);
void k_lifo_put(struct k_lifo *lifo, void *data);
void *k_lifo_get(struct k_lifo *lifo, k_timeout_t timeout);
int k_lifo_alloc_put(struct k_lifo *lifo, void *data);  /* Uses heap */
```

**Macros:**
- `K_LIFO_DEFINE(name)` - Static definition

#### Stack

```c
void k_stack_init(struct k_stack *stack, stack_data_t *buffer, uint32_t num_entries);
int k_stack_push(struct k_stack *stack, stack_data_t data);
int k_stack_pop(struct k_stack *stack, stack_data_t *data, k_timeout_t timeout);
int k_stack_alloc_init(struct k_stack *stack, uint32_t num_entries);  /* Uses heap */
int k_stack_cleanup(struct k_stack *stack);
```

**Macros:**
- `K_STACK_DEFINE(name, stack_num_entries)` - Static definition

#### Message Queue

```c
void k_msgq_init(struct k_msgq *msgq, char *buffer, size_t msg_size, uint32_t max_msgs);
int k_msgq_put(struct k_msgq *msgq, const void *data, k_timeout_t timeout);
int k_msgq_get(struct k_msgq *msgq, void *data, k_timeout_t timeout);
int k_msgq_peek(struct k_msgq *msgq, void *data);
int k_msgq_peek_at(struct k_msgq *msgq, void *data, uint32_t idx);
void k_msgq_purge(struct k_msgq *msgq);
uint32_t k_msgq_num_free_get(struct k_msgq *msgq);
uint32_t k_msgq_num_used_get(struct k_msgq *msgq);
int k_msgq_alloc_init(struct k_msgq *msgq, size_t msg_size, uint32_t max_msgs);
int k_msgq_cleanup(struct k_msgq *msgq);
```

**Macros:**
- `K_MSGQ_DEFINE(name, msg_size, max_msgs, align)` - Static definition

#### Mailbox

```c
void k_mbox_init(struct k_mbox *mbox);
int k_mbox_put(struct k_mbox *mbox, struct k_mbox_msg *tx_msg, k_timeout_t timeout);
void k_mbox_async_put(struct k_mbox *mbox, struct k_mbox_msg *tx_msg, struct k_sem *sem);
int k_mbox_get(struct k_mbox *mbox, struct k_mbox_msg *rx_msg, void *buffer, k_timeout_t timeout);
void k_mbox_data_get(struct k_mbox_msg *rx_msg, void *buffer);
```

**Macros:**
- `K_MBOX_DEFINE(name)` - Static definition

#### Pipe

```c
void k_pipe_init(struct k_pipe *pipe, unsigned char *buffer, size_t size);
int k_pipe_put(struct k_pipe *pipe, const void *data, size_t bytes_to_write,
               size_t *bytes_written, size_t min_xfer, k_timeout_t timeout);
int k_pipe_get(struct k_pipe *pipe, void *data, size_t bytes_to_read,
               size_t *bytes_read, size_t min_xfer, k_timeout_t timeout);
int k_pipe_alloc_init(struct k_pipe *pipe, size_t size);
int k_pipe_cleanup(struct k_pipe *pipe);
size_t k_pipe_read_avail(struct k_pipe *pipe);
size_t k_pipe_write_avail(struct k_pipe *pipe);
void k_pipe_flush(struct k_pipe *pipe);
void k_pipe_buffer_flush(struct k_pipe *pipe);
```

**Macros:**
- `K_PIPE_DEFINE(name, pipe_buffer_size, pipe_align)` - Static definition

#### ZBus

```c
int zbus_chan_pub(const struct zbus_channel *chan, const void *msg, k_timeout_t timeout);
int zbus_chan_read(const struct zbus_channel *chan, void *msg, k_timeout_t timeout);
int zbus_chan_claim(const struct zbus_channel *chan, k_timeout_t timeout);
int zbus_chan_finish(const struct zbus_channel *chan);
int zbus_chan_add_obs(const struct zbus_channel *chan, const struct zbus_observer *obs, k_timeout_t timeout);
int zbus_chan_rm_obs(const struct zbus_channel *chan, const struct zbus_observer *obs, k_timeout_t timeout);
int zbus_sub_wait(const struct zbus_observer *sub, const struct zbus_channel **chan, k_timeout_t timeout);
```

**Macros:**
- `ZBUS_CHAN_DEFINE(name, type, validator, user_data, observers, init_val)`
- `ZBUS_LISTENER_DEFINE(name, cb)`
- `ZBUS_MSG_SUBSCRIBER_DEFINE(name)`
- `ZBUS_SUBSCRIBER_DEFINE(name, queue_size)` (deprecated, use MSG_SUBSCRIBER)

#### Ring Buffer

```c
void ring_buf_init(struct ring_buf *buf, uint32_t size, uint8_t *data);
uint32_t ring_buf_put(struct ring_buf *buf, const uint8_t *data, uint32_t size);
uint32_t ring_buf_get(struct ring_buf *buf, uint8_t *data, uint32_t size);
uint32_t ring_buf_peek(struct ring_buf *buf, uint8_t *data, uint32_t size);
uint32_t ring_buf_put_claim(struct ring_buf *buf, uint8_t **data, uint32_t size);
int ring_buf_put_finish(struct ring_buf *buf, uint32_t size);
uint32_t ring_buf_get_claim(struct ring_buf *buf, uint8_t **data, uint32_t size);
int ring_buf_get_finish(struct ring_buf *buf, uint32_t size);
uint32_t ring_buf_space_get(struct ring_buf *buf);
uint32_t ring_buf_size_get(struct ring_buf *buf);
bool ring_buf_is_empty(struct ring_buf *buf);
void ring_buf_reset(struct ring_buf *buf);
```

**Macros:**
- `RING_BUF_DECLARE(name, size)` - Static definition

---

### Sample Directories

| Sample | Path |
| :--- | :--- |
| Message Queue | `<zephyr-ws-dir>/zephyr/samples/kernel/msg_queue` |
| Philosophers | `<zephyr-ws-dir>/zephyr/samples/philosophers` |
| ZBus Samples | `<zephyr-ws-dir>/zephyr/samples/subsys/zbus/` |

### Documentation Paths

| Topic | Path |
| :--- | :--- |
| Data Passing | `<zephyr-ws-dir>/zephyr/doc/kernel/services/data_passing/` |
| ZBus | `<zephyr-ws-dir>/zephyr/doc/services/zbus/` |

## Comparison

### Quick Reference Table

| Object | Bidirectional? | Data structure | Data item size | Data Alignment | ISRs can receive? | ISRs can send? | Overrun handling |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **FIFO** | No | Queue | Arbitrary | 4 B | Yes (No Wait) | Yes | N/A |
| **LIFO** | No | Queue | Arbitrary | 4 B | Yes (No Wait) | Yes | N/A |
| **Stack** | No | Array | Word | Word | Yes (No Wait) | Yes | Undefined behavior |
| **Message queue** | No | Ring buffer | Arbitrary | Power of two | Yes (No Wait) | Yes | Pend thread or return -errno |
| **Mailbox** | Yes | Queue | Arbitrary | Arbitrary | No | No | N/A |
| **Pipe** | No | Ring buffer (Optional) | Arbitrary | Arbitrary | Yes (No Wait) | Yes (No Wait) | Pend thread or return -errno |
| **ZBus** | No (Pub/Sub) | Channel | Arbitrary | Arbitrary | Yes (Publish) | Yes | Overwrites previous value |
| **Ring Buffer** | No | Circular array | Byte stream | None | Yes | Yes | Partial write / drop |

### Selection Guide

#### FIFO (First-In, First-Out)
*   **Use when**: You need to transfer data items of arbitrary size asynchronously in a FIFO manner.
*   **Pros**: Simple, efficient for processing data in order of arrival.
*   **Cons**: Requires callers to allocate space for queue overhead in data elements (unless using `k_fifo_alloc_put`).
*   **Avoid when**: You need fixed-size messages with built-in buffering (use Message Queue).

#### LIFO (Last-In, First-Out)
*   **Use when**: You need to transfer data items of arbitrary size asynchronously in a LIFO manner.
*   **Pros**: Good for "undo" operations or processing most recent data first.
*   **Cons**: Same allocation constraints as FIFO.
*   **Avoid when**: Order of arrival matters (use FIFO).

#### Stack
*   **Use when**: You need to transfer integer-sized data items in a LIFO manner.
*   **Pros**: Very low overhead, simple array-based implementation.
*   **Cons**: Fixed data size (machine word), potential for undefined behavior on overrun.
*   **Avoid when**: You need arbitrary-sized data (use LIFO) or FIFO ordering.

#### Message Queue
*   **Use when**: You need to transfer small, fixed-size data items asynchronously.
*   **Pros**: Ring buffer implementation avoids dynamic allocation per item. Can peek at data.
*   **Cons**: Data is copied (memcpy), so large items increase latency.
*   **Avoid when**: Data items are large (use FIFO with pointers or Mailbox).

#### Mailbox
*   **Use when**: You need enhanced capabilities beyond a message queue, such as:
    *   Synchronous transfer (block until received).
    *   Variable-sized messages.
    *   Bidirectional exchange (sender gets info back).
    *   Flow control.
*   **Pros**: Very flexible, supports "empty" signals and huge data transfers (by reference).
*   **Cons**: Heavier weight, ISRs cannot participate.
*   **Avoid when**: ISRs are involved or you need simple async transfer.

#### Pipe
*   **Use when**: You need to send a byte stream (chunks of data) between threads.
*   **Pros**: Handles partial reads/writes, ideal for stream processing (e.g., UART data).
*   **Cons**: More complex than simple queues.
*   **Avoid when**: You need discrete messages (use Message Queue).

#### ZBus (Zephyr Bus)
*   **Use when**:
    *   Decoupled, event-driven architecture.
    *   One-to-many or many-to-many communication.
    *   Broadcasting state changes to multiple subscribers.
*   **Pros**: Loose coupling, flexible observer patterns, supports listeners and message subscribers.
*   **Cons**: Higher overhead, subscribers may miss intermediate values (only latest retained).
*   **Avoid when**: You need guaranteed delivery of every message (use Message Queue).
*   **Details**: [#zbus](zbus.md)

#### Ring Buffer
*   **Use when**:
    *   Low-level byte stream buffering (drivers, protocol parsing).
    *   Performance-critical paths needing zero-copy access.
    *   ISR-to-thread data passing with manual synchronization.
*   **Pros**: Minimal overhead, zero-copy claim/finish API, SPSC lock-free.
*   **Cons**: No built-in blocking (must pair with semaphore/event), not thread-safe for MPMC.
*   **Avoid when**: You need kernel-managed blocking (use Pipe).
*   **Details**: [#ring_buffer](ring_buffer.md)

### Decision Flowchart

```
Need to pass data between threads/ISRs?
│
├─ Is it a byte stream (not discrete messages)?
│  ├─ Yes, need kernel blocking → Pipe
│  └─ Yes, need minimal overhead → Ring Buffer + Semaphore
│
├─ Is it pub/sub (1:N, N:1, event-driven)?
│  └─ Yes → ZBus
│
├─ Fixed-size messages?
│  ├─ Yes, small items → Message Queue
│  └─ Yes, but large or need zero-copy → FIFO with pointers
│
├─ Variable-size messages?
│  ├─ Need bidirectional / sync → Mailbox
│  └─ Async, arbitrary order → FIFO or LIFO
│
├─ Integer values only (machine word)?
│  └─ Yes, LIFO order → Stack
│
└─ Processing order?
   ├─ First-in, first-out → FIFO
   └─ Last-in, first-out → LIFO
```

## Data Passing

This reference details the specific implementation and usage of Zephyr kernel data passing objects.

### FIFO (First-In, First-Out)

A FIFO implements a traditional queue, allowing threads and ISRs to add and remove data items of any size.

#### Key Concepts
*   **Structure**: Linked list of data items.
*   **Memory**: Caller usually provides memory. 1st word of data item reserved for kernel use (pointer to next).
*   **Alignment**: Data items must be aligned on word boundary.
*   **Allocation**: `k_fifo_alloc_put` can be used to use a resource pool instead of embedded pointers.

#### Implementation

**Definition**:
```c
struct k_fifo my_fifo;
k_fifo_init(&my_fifo);
// OR
K_FIFO_DEFINE(my_fifo);
```

**Writing (Producer)**:
```c
struct data_item_t {
    void *fifo_reserved; /* 1st word reserved */
    uint32_t value;
};
struct data_item_t tx_data;
k_fifo_put(&my_fifo, &tx_data);
```

**Reading (Consumer)**:
```c
struct data_item_t *rx_data;
rx_data = k_fifo_get(&my_fifo, K_FOREVER);
```

---

### LIFO (Last-In, First-Out)

A LIFO is similar to a FIFO but processes data in reverse order (stack-like behavior for arbitrary data).

#### Key Concepts

*   **Structure**: Linked list of data items (same as FIFO).
*   **Memory**: Caller provides memory. 1st word of data item reserved for kernel use (pointer to next).
*   **Alignment**: Data items must be aligned on word boundary.
*   **Allocation**: `k_lifo_alloc_put` can use a resource pool instead of embedded pointers.
*   **Use Case**: "Undo" operations, processing most recent data first, memory pool management.

#### Implementation

**Definition**:
```c
struct k_lifo my_lifo;
k_lifo_init(&my_lifo);
// OR
K_LIFO_DEFINE(my_lifo);
```

**Writing (Producer)**:
```c
struct data_item_t {
    void *lifo_reserved; /* 1st word reserved */
    uint32_t value;
};
struct data_item_t tx_data;
k_lifo_put(&my_lifo, &tx_data);
```

**Reading (Consumer)**:
```c
struct data_item_t *rx_data;
rx_data = k_lifo_get(&my_lifo, K_FOREVER);
// Returns most recently added item
```

**Heap Allocation (no embedded pointer)**:
```c
struct data_item_t *item = k_malloc(sizeof(*item));
item->value = 42;
k_lifo_alloc_put(&my_lifo, item);  // Kernel manages linkage
```

---

### Stack

A Stack allows threads and ISRs to exchange integer-sized values (machine words) in a LIFO manner.

#### Key Concepts
*   **Structure**: Array of machine words.
*   **Limit**: Fixed capacity. Overrun causes undefined behavior.

#### Implementation

**Definition**:
```c
k_stack_stack_t my_stack_data[20]; // Array to hold data
struct k_stack my_stack;
k_stack_init(&my_stack, my_stack_data, 20);
// OR
K_STACK_DEFINE(my_stack, 20);
```

**Writing**: `k_stack_push(&my_stack, data_val);`
**Reading**: `k_stack_pop(&my_stack, &rx_val, K_FOREVER);`

---

### Message Queue

A Message Queue allows asynchronous exchange of fixed-size data items via a ring buffer.

#### Key Concepts
*   **Structure**: Ring buffer.
*   **Data**: Fixed size per item.
*   **Copying**: Data is copied into the buffer (put) and out of it (get). No internal pointers exposed.

#### Implementation

**Definition**:
```c
struct k_msgq my_msgq;
char my_msgq_buffer[10 * sizeof(struct data_item)];
k_msgq_init(&my_msgq, my_msgq_buffer, sizeof(struct data_item), 10);
// OR
K_MSGQ_DEFINE(my_msgq, sizeof(struct data_item), 10, 4); // 4 is alignment
```

**Writing**: `k_msgq_put(&my_msgq, &data, K_NO_WAIT);` (returns non-zero if full)
**Reading**: `k_msgq_get(&my_msgq, &data, K_FOREVER);`
**Peeking**: `k_msgq_peek(&my_msgq, &data);`
**Purging**: `k_msgq_purge(&my_msgq);` (clears queue)

---

### Mailbox

A Mailbox provides synchronous or asynchronous exchange of variable-sized messages.

#### Key Concepts
*   **Bidirectional**: Sender can get info back.
*   **Flow Control**: Synchronous mode blocks sender until received.
*   **Efficiency**: Can transfer large data by reference (pointer) to avoid copying.
*   **No ISRs**: Only threads can use mailboxes.

#### Implementation

**Definition**:
```c
struct k_mbox my_mbox;
k_mbox_init(&my_mbox);
// OR
K_MBOX_DEFINE(my_mbox);
```

**Sending**:
```c
struct k_mbox_msg send_msg;
send_msg.info = 123; // App-specific info
send_msg.size = size;
send_msg.tx_data = buffer;
send_msg.tx_target_thread = K_ANY;
k_mbox_put(&my_mbox, &send_msg, K_FOREVER);
```

**Receiving**:
```c
struct k_mbox_msg recv_msg;
recv_msg.size = max_size;
recv_msg.rx_source_thread = K_ANY;
k_mbox_get(&my_mbox, &recv_msg, buffer, K_FOREVER);
```

---

### Pipe

A Pipe allows a byte stream (chunks of data) to be sent between threads.

#### Key Concepts
*   **Stream**: Data is treated as a stream of bytes, not discrete messages.
*   **Partial Access**: Can read/write fewer bytes than requested if buffer full/empty.
*   **Ring Buffer**: Optional internal buffer. If 0 size, pipe is purely synchronous (direct copy from sender to receiver).

#### Implementation

**Definition**:
```c
unsigned char my_ring_buffer[100];
struct k_pipe my_pipe;
k_pipe_init(&my_pipe, my_ring_buffer, sizeof(my_ring_buffer));
// OR
K_PIPE_DEFINE(my_pipe, 100, 4);
```

**Writing**:
```c
size_t bytes_written;
int ret = k_pipe_write(&my_pipe, data, total_size, &bytes_written, min_xfer, K_NO_WAIT);
```

**Reading**:
```c
size_t bytes_read;
int ret = k_pipe_read(&my_pipe, buffer, bytes_to_read, &bytes_read, min_xfer, K_FOREVER);
```

---

### Common Pitfalls and Best Practices

#### Memory and Alignment

| Pitfall | Solution |
| :--- | :--- |
| Forgetting reserved word in FIFO/LIFO data structure | Always include `void *reserved;` as first member |
| Unaligned data items | Use `__aligned(4)` or ensure struct padding |
| Using stack-allocated items that go out of scope | Use static/heap allocation or ensure lifetime |
| Stack overflow (k_stack) | Check return value of `k_stack_push` or size appropriately |

#### ISR Usage

| Pitfall | Solution |
| :--- | :--- |
| Using blocking calls in ISR | Always use `K_NO_WAIT` in ISR context |
| Using Mailbox from ISR | Mailboxes are thread-only; use FIFO/MsgQ instead |
| Missing data in high-frequency ISR | Increase buffer size or use ring buffer |

#### Performance

| Pitfall | Solution |
| :--- | :--- |
| Large memcpy in Message Queue | Use FIFO with pointers for large data |
| Unnecessary copying | Use Mailbox with `tx_data` pointer for zero-copy |
| Blocking on full queue | Use `K_NO_WAIT` and handle `-ENOMSG`/`-EAGAIN` |

#### Synchronization

| Pitfall | Solution |
| :--- | :--- |
| Race condition on shared data | Data passing objects are thread-safe; protect additional shared state |
| Lost wakeups | Use proper timeout handling, check return values |
| Priority inversion with data passing | Consider message priority or use Mailbox targeted threads |

#### Debugging Tips

1.  **Check return values**: All blocking calls return error codes on timeout/failure.
2.  **Use `k_msgq_num_used_get()`**: Monitor queue depth for sizing issues.
3.  **Enable `CONFIG_ASSERT`**: Catches many usage errors at runtime.
4.  **Use `CONFIG_ZBUS_CHANNEL_NAME`**: For ZBus debugging, enables channel name printing.
5.  **Tracing**: Enable Zephyr tracing to visualize data flow between threads.

## Ring Buffer

A Ring Buffer (circular buffer) is a lower-level data structure for efficient byte-stream handling, commonly used in drivers and protocol stacks.

### Concepts

-   **Structure**: Fixed-size byte array with head and tail pointers.
-   **Library-level**: Part of `sys/` utilities, not a kernel object (no built-in blocking).
-   **Lock-free**: Single-producer/single-consumer (SPSC) operations are lock-free.
-   **No Waiting**: Does not support thread blocking; use with semaphores/events for synchronization.

### When to Use Ring Buffer

-   **Driver buffers**: UART RX/TX, SPI, I2C byte streams.
-   **Protocol parsing**: Accumulate incoming bytes until a complete frame.
-   **Performance-critical paths**: Lower overhead than kernel message queues.
-   **ISR-to-thread**: Buffer data in ISR, process in thread (with separate signaling).

### Kconfig

Ring buffers are always available (part of core library). No specific Kconfig needed.

### Implementation

#### Including

```c
#include <zephyr/sys/ring_buffer.h>
```

#### Defining

```c
/* Static definition with compile-time buffer */
RING_BUF_DECLARE(my_ring_buf, 256);

/* OR runtime initialization */
uint8_t buffer[256];
struct ring_buf my_ring_buf;
ring_buf_init(&my_ring_buf, sizeof(buffer), buffer);
```

#### Writing (Producer)

```c
uint8_t data[] = {0x01, 0x02, 0x03};
uint32_t written = ring_buf_put(&my_ring_buf, data, sizeof(data));
if (written < sizeof(data)) {
    /* Buffer full, only partial write */
}
```

#### Reading (Consumer)

```c
uint8_t rx_data[64];
uint32_t read = ring_buf_get(&my_ring_buf, rx_data, sizeof(rx_data));
/* read contains number of bytes actually retrieved */
```

#### Zero-Copy Access (Advanced)

For high-performance scenarios, claim buffer space directly:

```c
/* Producer: claim space */
uint8_t *ptr;
uint32_t space = ring_buf_put_claim(&my_ring_buf, &ptr, 100);
/* Write directly to ptr (up to 'space' bytes) */
memcpy(ptr, source_data, actual_len);
ring_buf_put_finish(&my_ring_buf, actual_len);

/* Consumer: get data pointer */
uint8_t *ptr;
uint32_t available = ring_buf_get_claim(&my_ring_buf, &ptr, 100);
/* Read directly from ptr */
process_data(ptr, available);
ring_buf_get_finish(&my_ring_buf, available);
```

#### Querying State

```c
uint32_t free_space = ring_buf_space_get(&my_ring_buf);
uint32_t used_space = ring_buf_size_get(&my_ring_buf);
bool is_empty = ring_buf_is_empty(&my_ring_buf);
ring_buf_reset(&my_ring_buf);  /* Clear buffer */
```

### Key APIs

| Function | Description |
| :--- | :--- |
| `RING_BUF_DECLARE` | Static buffer definition |
| `ring_buf_init` | Runtime initialization |
| `ring_buf_put` | Write bytes (copy) |
| `ring_buf_get` | Read bytes (copy) |
| `ring_buf_put_claim` / `ring_buf_put_finish` | Zero-copy write |
| `ring_buf_get_claim` / `ring_buf_get_finish` | Zero-copy read |
| `ring_buf_peek` | Read without consuming |
| `ring_buf_space_get` | Get free space |
| `ring_buf_size_get` | Get used space |
| `ring_buf_reset` | Clear the buffer |
| `ring_buf_is_empty` | Check if empty |

### Comparison with Kernel Pipe

| Aspect | Ring Buffer | Pipe |
| :--- | :--- | :--- |
| **Blocking** | No (manual sync needed) | Yes (kernel-managed) |
| **ISR Safe** | Yes (no blocking calls) | Yes (with K_NO_WAIT) |
| **Overhead** | Minimal | Higher (kernel object) |
| **Use Case** | Drivers, low-level | Application-level streams |
| **Zero-Copy** | Yes (claim/finish) | No |
| **Thread Safety** | SPSC lock-free; MPMC needs external lock | Built-in |

### Common Patterns

#### UART RX with Ring Buffer

```c
RING_BUF_DECLARE(uart_rx_buf, 512);
K_SEM_DEFINE(uart_rx_sem, 0, 1);

/* ISR callback */
void uart_isr_callback(const struct device *dev, void *user_data)
{
    uint8_t byte;
    while (uart_fifo_read(dev, &byte, 1) > 0) {
        ring_buf_put(&uart_rx_buf, &byte, 1);
    }
    k_sem_give(&uart_rx_sem);  /* Signal data available */
}

/* Thread */
void uart_thread(void)
{
    uint8_t data[64];
    while (1) {
        k_sem_take(&uart_rx_sem, K_FOREVER);
        uint32_t len = ring_buf_get(&uart_rx_buf, data, sizeof(data));
        process_uart_data(data, len);
    }
}
```

### Best Practices

1.  **Size appropriately**: Buffer should handle worst-case burst without overflow.
2.  **Use zero-copy for performance**: Avoid memcpy when possible.
3.  **Add synchronization for MPMC**: Use mutex for multiple producers/consumers.
4.  **Pair with semaphore/event**: For thread blocking on data availability.
5.  **Check return values**: Partial reads/writes are common.

## Zbus

ZBus is a lightweight publish-subscribe inter-process communication framework for Zephyr.

### Concepts

-   **Channel**: Named message buffer with a defined message type. The central entity for communication.
-   **Publisher**: Any context (thread, ISR, work queue) that sends messages to a channel.
-   **Subscriber**: Observer that receives notifications when a channel is updated.
-   **Listener**: Callback-based subscriber (runs in publisher's context).
-   **Message Subscriber**: Thread-based subscriber with its own message queue.
-   **Observation**: The act of a subscriber watching a channel.

### When to Use ZBus

-   **Decoupled communication**: Publishers and subscribers don't need to know each other.
-   **One-to-many**: Single publisher, multiple subscribers.
-   **Many-to-one**: Multiple publishers, single subscriber.
-   **Event-driven architecture**: React to state changes across the system.
-   **Sensor data distribution**: Broadcast sensor readings to multiple consumers.

### Kconfig

```kconfig
CONFIG_ZBUS=y                    # Enable ZBus
CONFIG_ZBUS_CHANNEL_NAME=y       # Store channel names (for debugging)
CONFIG_ZBUS_RUNTIME_OBSERVERS=y  # Allow dynamic observer registration
```

### Implementation

#### Defining a Channel

```c
#include <zephyr/zbus/zbus.h>

struct sensor_data {
    int temperature;
    int humidity;
};

/* Define a channel with initial value */
ZBUS_CHAN_DEFINE(sensor_chan,           /* Channel name */
                 struct sensor_data,     /* Message type */
                 NULL,                   /* Optional validator */
                 NULL,                   /* Optional user data */
                 ZBUS_OBSERVERS_EMPTY,   /* Static observers (or list) */
                 ZBUS_MSG_INIT(.temperature = 0, .humidity = 0)  /* Initial value */
);
```

#### Publishing

```c
struct sensor_data data = {.temperature = 25, .humidity = 60};

/* Publish with timeout */
int ret = zbus_chan_pub(&sensor_chan, &data, K_MSEC(100));
if (ret != 0) {
    /* Handle publish failure (channel busy, timeout) */
}
```

#### Subscribing (Listener - Callback)

Listeners execute in the publisher's context. Keep them short.

```c
void sensor_listener_cb(const struct zbus_channel *chan)
{
    struct sensor_data data;
    zbus_chan_read(chan, &data, K_NO_WAIT);
    printk("Temp: %d, Humidity: %d\n", data.temperature, data.humidity);
}

ZBUS_LISTENER_DEFINE(my_listener, sensor_listener_cb);

/* Static observation (at compile time) */
ZBUS_CHAN_DEFINE(sensor_chan, ...,
                 ZBUS_OBSERVERS(my_listener),  /* Add listener here */
                 ...);

/* OR runtime observation */
zbus_chan_add_obs(&sensor_chan, &my_listener, K_FOREVER);
```

#### Subscribing (Message Subscriber - Thread)

Message subscribers have their own FIFO and process messages in their own thread context.

```c
ZBUS_MSG_SUBSCRIBER_DEFINE(my_msg_sub);

/* Add to channel observers */
ZBUS_CHAN_ADD_OBS(sensor_chan, my_msg_sub, 3);  /* priority 3 */

void subscriber_thread(void)
{
    const struct zbus_channel *chan;
    struct sensor_data data;

    while (1) {
        /* Wait for any observed channel to be published */
        if (zbus_sub_wait(&my_msg_sub, &chan, K_FOREVER) == 0) {
            zbus_chan_read(chan, &data, K_NO_WAIT);
            /* Process data */
        }
    }
}
```

#### Reading Channel (Without Subscribing)

```c
struct sensor_data data;
zbus_chan_read(&sensor_chan, &data, K_MSEC(100));
```

### Key APIs

| Function | Description |
| :--- | :--- |
| `ZBUS_CHAN_DEFINE` | Define a channel at compile time |
| `zbus_chan_pub` | Publish a message to a channel |
| `zbus_chan_read` | Read current channel value |
| `zbus_chan_claim` / `zbus_chan_finish` | Claim exclusive access for read-modify-write |
| `ZBUS_LISTENER_DEFINE` | Define a callback-based observer |
| `ZBUS_MSG_SUBSCRIBER_DEFINE` | Define a thread-based observer |
| `zbus_sub_wait` | Wait for a message (for msg subscribers) |
| `zbus_chan_add_obs` / `zbus_chan_rm_obs` | Runtime observer management |

### Comparison with Traditional Objects

| Aspect | ZBus | Message Queue |
| :--- | :--- | :--- |
| **Pattern** | Pub/Sub (1:N, N:1, N:M) | Point-to-point (1:1) |
| **Coupling** | Loose (via channel name) | Tight (direct queue reference) |
| **Delivery** | Latest value (can miss updates) | Queued (all messages buffered) |
| **ISR Safe** | Yes (publish) | Yes (with K_NO_WAIT) |
| **Overhead** | Higher (observer management) | Lower |

### Best Practices

1.  **Keep listeners short**: They run in publisher context.
2.  **Use message subscribers for heavy processing**: They have their own thread.
3.  **Validate messages**: Use the validator callback to reject invalid data.
4.  **Use `zbus_chan_claim`/`finish`**: For atomic read-modify-write operations.
5.  **Consider memory**: Each channel stores one message; message subscribers add FIFO overhead.
