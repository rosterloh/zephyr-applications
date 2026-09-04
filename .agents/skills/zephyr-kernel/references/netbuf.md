# Network Buffers (net_buf)

## Overview

This skill provides expert knowledge on Zephyr's `net_buf` subsystem — a core memory management library used by networking, Bluetooth, and USB stacks. It covers pool creation, buffer lifecycle, data manipulation, and fragmentation patterns.

### Key Concepts

**Two Buffer Types:**
- `net_buf_simple` — Lightweight, no reference counting, stack-allocatable. Use for constrained scenarios.
- `net_buf` — Full-featured with reference counting, fragmentation, user data. Use for passing through kernel objects (FIFOs).

**Buffer Layout:**
```
[__buf start] <-- headroom --> [data] <-- len --> [tailroom] <-- [__buf end]
                                  ^
                               data pointer
```

### Workflow

#### 1. Choose Buffer Type

| Scenario | Use |
|----------|-----|
| Temporary parsing, stack allocation | `net_buf_simple` + `NET_BUF_SIMPLE_DEFINE` |
| Pool-based allocation, ref counting | `net_buf` + `NET_BUF_POOL_DEFINE` |
| Large/fragmented packets | `net_buf` with fragment chains |

#### 2. Pool Definition

For `net_buf`, define a pool at file scope:

```c
#include <zephyr/net_buf.h>

/* Fixed-size buffers (most common) */
NET_BUF_POOL_DEFINE(my_pool,
    10,    /* count: number of buffers */
    128,   /* size: max data per buffer */
    4,     /* user_data_size: metadata per buffer */
    NULL); /* destroy callback (optional) */
```

**Pool variants**: See [#api](#api) for `NET_BUF_POOL_VAR_DEFINE` (variable size) and `NET_BUF_POOL_HEAP_DEFINE` (heap-backed).

#### 3. Allocation & Lifecycle

```c
/* Allocate (blocks until available or timeout) */
struct net_buf *buf = net_buf_alloc(&my_pool, K_FOREVER);

/* Reserve headroom for protocol headers */
net_buf_reserve(buf, HEADER_SIZE);

/* ... use buffer ... */

/* Release (returns to pool when refcount hits 0) */
net_buf_unref(buf);
```

**Reference counting:**
- `net_buf_alloc()` → refcount = 1
- `net_buf_ref(buf)` → increments refcount
- `net_buf_unref(buf)` → decrements; frees when 0

#### 4. Data Manipulation

Four operation types — choose based on direction:

| Operation | Direction | Pointer | Length | Use Case |
|-----------|-----------|---------|--------|----------|
| **Add** | End ↓ | Unchanged | +len | Append payload |
| **Remove** | End ↑ | Unchanged | -len | Trim from tail |
| **Push** | Start ← | Moves back | +len | Prepend header |
| **Pull** | Start → | Moves forward | -len | Parse/consume header |

**Step 4:** Read [#operations](#operations) for:
- Endian-aware helpers (`_le16`, `_be32`, etc.)
- Byte-by-byte protocol parsing patterns
- Common encoding/decoding examples

#### 5. Fragmentation (Advanced)

For packets larger than a single buffer:

```c
struct net_buf *head = net_buf_alloc(&pool, K_FOREVER);
struct net_buf *frag = net_buf_alloc(&pool, K_FOREVER);

/* Add fragment to chain */
net_buf_frag_add(head, frag);

/* Iterate fragments */
for (struct net_buf *f = head; f; f = f->frags) {
    /* process f->data, f->len */
}

/* Total length across all fragments */
size_t total = net_buf_frags_len(head);
```

See [#fragmentation](#fragmentation) for advanced patterns.

#### 6. API & Configuration

**Step 6:** Read [#api](#api) for:
- Complete API function signatures
- Kconfig options (`CONFIG_NET_BUF_LOG`, `CONFIG_NET_BUF_POOL_USAGE`)
- Pool definition macro variants

### Common Pitfalls

- **Forgetting headroom**: Call `net_buf_reserve()` immediately after allocation if you need to prepend headers later.
- **Double unref**: Each `alloc` or `ref` needs exactly one `unref`. Track ownership carefully.
- **ISR allocation**: Use `K_NO_WAIT` in ISRs and handle NULL returns.
- **Blocking in destroy callback**: The destroy callback runs with a spinlock held — keep it fast.

### Source Locations

| Description | Path |
|:---|:---|
| **net_buf Header** | `<zephyr-ws>/deps/zephyr/include/zephyr/net_buf.h` |
| **net_buf Implementation** | `<zephyr-ws>/deps/zephyr/lib/net_buf/buf.c` |
| **Documentation** | `<zephyr-ws>/deps/zephyr/doc/services/net_buf/index.rst` |
| **UART Async Sample** | `<zephyr-ws>/deps/zephyr/samples/drivers/uart/async_api/` |
| **Bluetooth Samples** | `<zephyr-ws>/deps/zephyr/samples/bluetooth/` |

## Api

### Header File

```c
#include <zephyr/net_buf.h>
```

### Pool Definition Macros

#### NET_BUF_POOL_DEFINE (Fixed-Size)

Most common. Allocates fixed-size data chunks.

```c
NET_BUF_POOL_DEFINE(name, count, size, ud_size, destroy);
```

| Parameter | Description |
|-----------|-------------|
| `name` | Pool variable name |
| `count` | Number of buffers in pool |
| `size` | Max data bytes per buffer |
| `ud_size` | User data size per buffer (0 if unused) |
| `destroy` | Optional callback when buffer freed (NULL if unused) |

#### NET_BUF_POOL_VAR_DEFINE (Variable-Size)

Uses a heap for variable-sized allocations.

```c
NET_BUF_POOL_VAR_DEFINE(name, count, total_data_size, ud_size, destroy);
```

- `total_data_size` — Total heap size for all buffer data.

#### NET_BUF_POOL_HEAP_DEFINE (System Heap)

Uses `k_malloc` for data. Requires `CONFIG_HEAP_MEM_POOL_SIZE > 0`.

```c
NET_BUF_POOL_HEAP_DEFINE(name, count, ud_size, destroy);
```

**Note**: Does not support blocking on allocation — always treated as `K_NO_WAIT`.

#### NET_BUF_SIMPLE_DEFINE (Stack)

Defines a `net_buf_simple` on the stack (no pool).

```c
NET_BUF_SIMPLE_DEFINE(name, size);
```

Or get a pointer directly:

```c
struct net_buf_simple *buf = NET_BUF_SIMPLE(size);
net_buf_simple_init(buf, reserve_headroom);
```

---

### Lifecycle Functions

#### Allocation

```c
struct net_buf *net_buf_alloc(struct net_buf_pool *pool, k_timeout_t timeout);
struct net_buf *net_buf_alloc_len(struct net_buf_pool *pool, size_t size, k_timeout_t timeout);
struct net_buf *net_buf_alloc_with_data(struct net_buf_pool *pool, void *data, size_t size, k_timeout_t timeout);
```

| Function | Use Case |
|----------|----------|
| `net_buf_alloc` | Fixed-size pools |
| `net_buf_alloc_len` | Variable-size pools (specify needed size) |
| `net_buf_alloc_with_data` | Use external data pointer |

#### Reference Counting

```c
struct net_buf *net_buf_ref(struct net_buf *buf);   /* Increment refcount */
void net_buf_unref(struct net_buf *buf);            /* Decrement; frees when 0 */
```

#### Reset & Clone

```c
void net_buf_reset(struct net_buf *buf);                              /* Reset data/flags for reuse */
struct net_buf *net_buf_clone(struct net_buf *buf, k_timeout_t timeout); /* Deep copy */
```

#### Headroom Reservation

```c
void net_buf_reserve(struct net_buf *buf, size_t reserve);
```

---

### Data Manipulation Functions

#### Add (Append to End)

```c
void *net_buf_add(struct net_buf *buf, size_t len);
void *net_buf_add_mem(struct net_buf *buf, const void *mem, size_t len);
uint8_t *net_buf_add_u8(struct net_buf *buf, uint8_t val);
void net_buf_add_le16(struct net_buf *buf, uint16_t val);
void net_buf_add_be16(struct net_buf *buf, uint16_t val);
void net_buf_add_le32(struct net_buf *buf, uint32_t val);
void net_buf_add_be32(struct net_buf *buf, uint32_t val);
void net_buf_add_le64(struct net_buf *buf, uint64_t val);
void net_buf_add_be64(struct net_buf *buf, uint64_t val);
```

#### Remove (Trim from End)

```c
void *net_buf_remove_mem(struct net_buf *buf, size_t len);
uint8_t net_buf_remove_u8(struct net_buf *buf);
uint16_t net_buf_remove_le16(struct net_buf *buf);
uint16_t net_buf_remove_be16(struct net_buf *buf);
uint32_t net_buf_remove_le32(struct net_buf *buf);
uint32_t net_buf_remove_be32(struct net_buf *buf);
```

#### Push (Prepend to Start)

```c
void *net_buf_push(struct net_buf *buf, size_t len);
void *net_buf_push_mem(struct net_buf *buf, const void *mem, size_t len);
void net_buf_push_u8(struct net_buf *buf, uint8_t val);
void net_buf_push_le16(struct net_buf *buf, uint16_t val);
void net_buf_push_be16(struct net_buf *buf, uint16_t val);
void net_buf_push_le32(struct net_buf *buf, uint32_t val);
void net_buf_push_be32(struct net_buf *buf, uint32_t val);
```

#### Pull (Consume from Start)

```c
void *net_buf_pull(struct net_buf *buf, size_t len);
void *net_buf_pull_mem(struct net_buf *buf, size_t len);
uint8_t net_buf_pull_u8(struct net_buf *buf);
uint16_t net_buf_pull_le16(struct net_buf *buf);
uint16_t net_buf_pull_be16(struct net_buf *buf);
uint32_t net_buf_pull_le32(struct net_buf *buf);
uint32_t net_buf_pull_be32(struct net_buf *buf);
uint64_t net_buf_pull_le64(struct net_buf *buf);
uint64_t net_buf_pull_be64(struct net_buf *buf);
```

---

### Buffer Info Functions

```c
size_t net_buf_tailroom(const struct net_buf *buf);  /* Free space at end */
size_t net_buf_headroom(const struct net_buf *buf);  /* Free space at start */
uint16_t net_buf_max_len(const struct net_buf *buf); /* Max usable length */
uint8_t *net_buf_tail(const struct net_buf *buf);    /* Pointer to end of data */
void *net_buf_user_data(const struct net_buf *buf);  /* Pointer to user_data */
int net_buf_id(const struct net_buf *buf);           /* Zero-based index in pool */
```

---

### Fragment Functions

```c
struct net_buf *net_buf_frag_last(struct net_buf *frags);
struct net_buf *net_buf_frag_add(struct net_buf *head, struct net_buf *frag);
void net_buf_frag_insert(struct net_buf *parent, struct net_buf *frag);
struct net_buf *net_buf_frag_del(struct net_buf *parent, struct net_buf *frag);
size_t net_buf_frags_len(const struct net_buf *buf);
size_t net_buf_linearize(void *dst, size_t dst_len, const struct net_buf *src, size_t offset, size_t len);
```

---

### List Operations

For passing buffers through `k_fifo`:

```c
/* Standard kernel FIFO works directly */
k_fifo_put(&my_fifo, buf);
buf = k_fifo_get(&my_fifo, K_FOREVER);
```

For `sys_slist_t`:

```c
void net_buf_slist_put(sys_slist_t *list, struct net_buf *buf);
struct net_buf *net_buf_slist_get(sys_slist_t *list);
```

---

### Kconfig Options

| Option | Description |
|--------|-------------|
| `CONFIG_NET_BUF_LOG` | Enable debug logging for net_buf operations |
| `CONFIG_NET_BUF_POOL_USAGE` | Track pool usage statistics (avail_count, max_used) |
| `CONFIG_NET_BUF_ALIGNMENT` | Data alignment (default: sizeof(void *)) |

---

### net_buf_simple API

Same operations as `net_buf` but with `_simple` suffix:

```c
void net_buf_simple_init(struct net_buf_simple *buf, size_t reserve_head);
void net_buf_simple_reset(struct net_buf_simple *buf);
void *net_buf_simple_add(struct net_buf_simple *buf, size_t len);
void *net_buf_simple_push(struct net_buf_simple *buf, size_t len);
void *net_buf_simple_pull(struct net_buf_simple *buf, size_t len);
size_t net_buf_simple_headroom(const struct net_buf_simple *buf);
size_t net_buf_simple_tailroom(const struct net_buf_simple *buf);
```

Use `net_buf_simple` when:
- Buffer is stack-allocated or has known lifetime
- No need for reference counting
- No need to pass through kernel objects

## Fragmentation

When a single buffer cannot hold all data (e.g., large packets, MTU limits), `net_buf` supports **fragment chains** — linked lists of buffers that together represent one logical packet.

```
┌─────────┐    ┌─────────┐    ┌─────────┐
│  head   │───►│  frag1  │───►│  frag2  │───► NULL
│ (buf)   │    │ (buf)   │    │ (buf)   │
│ frags ──┼────┘ frags ──┼────┘ frags   │
└─────────┘    └─────────┘    └─────────┘
```

Each `net_buf` has a `frags` pointer to the next fragment.

---

### Creating Fragment Chains

#### Method 1: net_buf_frag_add (Append)

Adds fragment to end of chain. Returns head.

```c
struct net_buf *head = net_buf_alloc(&pool, K_FOREVER);
net_buf_add_mem(head, data1, len1);

struct net_buf *frag = net_buf_alloc(&pool, K_FOREVER);
net_buf_add_mem(frag, data2, len2);

/* Add to chain — frag's ref is consumed */
head = net_buf_frag_add(head, frag);
```

**Important**: `net_buf_frag_add` takes ownership of the fragment reference. Do not `unref` it separately.

#### Method 2: net_buf_frag_insert (Insert After)

Inserts fragment after a specific parent buffer.

```c
struct net_buf *parent = /* ... */;
struct net_buf *new_frag = net_buf_alloc(&pool, K_FOREVER);

/* Insert new_frag immediately after parent */
net_buf_frag_insert(parent, new_frag);
/* Chain: parent → new_frag → (old parent->frags) */
```

---

### Traversing Fragment Chains

#### Simple Iteration

```c
for (struct net_buf *f = head; f != NULL; f = f->frags) {
    /* Process f->data (length f->len) */
    process_fragment(f->data, f->len);
}
```

#### Get Total Length

```c
size_t total = net_buf_frags_len(head);
```

#### Find Last Fragment

```c
struct net_buf *last = net_buf_frag_last(head);
/* last->frags == NULL */
```

---

### Linearizing Fragment Data

Copy fragmented data into a contiguous buffer:

```c
uint8_t linear_buf[1024];

/* Copy from offset 0, up to sizeof(linear_buf) bytes */
size_t copied = net_buf_linearize(linear_buf, sizeof(linear_buf),
                                   head, 0, net_buf_frags_len(head));
```

#### Parameters

| Param | Description |
|-------|-------------|
| `dst` | Destination buffer |
| `dst_len` | Max bytes to copy |
| `src` | Head of fragment chain |
| `offset` | Starting offset in chain |
| `len` | Max bytes to copy from chain |

Returns actual bytes copied.

---

### Removing Fragments

#### Delete Fragment from Chain

```c
/* parent is the buffer before frag, or NULL if frag is head */
struct net_buf *next = net_buf_frag_del(parent, frag);
/* frag is unreferenced; next is the fragment that followed it */
```

#### Delete First Fragment (Head)

```c
struct net_buf *old_head = head;
head = net_buf_frag_del(NULL, head);
/* old_head is unreferenced; head now points to former second fragment */
```

---

### Freeing Fragment Chains

`net_buf_unref` on head automatically frees the entire chain:

```c
net_buf_unref(head);
/* All fragments in chain are unreferenced */
```

**Caution**: If you hold references to individual fragments, they may become invalid after freeing head.

---

### Skip / Consume Across Fragments

The `net_buf_skip` helper advances through data, auto-deleting consumed fragments:

```c
/* Skip len bytes across fragment chain */
buf = net_buf_skip(buf, bytes_to_skip);
/* Returns updated head (or NULL if all data consumed) */
```

Internally:
1. Calls `net_buf_pull_u8` repeatedly
2. When a fragment is emptied (`len == 0`), deletes it
3. Moves to next fragment

---

### Appending Data Across Fragments

Use `net_buf_append_bytes` for large data that may span fragments:

```c
size_t added = net_buf_append_bytes(head, total_len, data,
                                     K_FOREVER,
                                     allocator_cb,  /* or NULL for same pool */
                                     user_data);
```

The function:
1. Fills remaining tailroom in current fragment
2. Allocates new fragments as needed
3. Returns bytes actually added

---

### Common Patterns

#### Pattern 1: Scatter-Gather TX

```c
struct net_buf *pkt = net_buf_alloc(&pool, K_FOREVER);
net_buf_reserve(pkt, HEADER_RESERVE);

/* Add header */
net_buf_push_mem(pkt, header, header_len);

/* Add payload fragments */
for (int i = 0; i < payload_count; i++) {
    struct net_buf *frag = net_buf_alloc(&pool, K_FOREVER);
    net_buf_add_mem(frag, payloads[i].data, payloads[i].len);
    net_buf_frag_add(pkt, frag);
}

/* Send entire chain */
send_packet(pkt);
```

#### Pattern 2: RX Reassembly

```c
static struct net_buf *rx_chain = NULL;

void on_rx_fragment(const uint8_t *data, size_t len)
{
    struct net_buf *frag = net_buf_alloc(&pool, K_NO_WAIT);
    if (!frag) return;

    net_buf_add_mem(frag, data, len);

    if (rx_chain == NULL) {
        rx_chain = frag;
    } else {
        net_buf_frag_add(rx_chain, frag);
    }
}

void on_rx_complete(void)
{
    /* Process complete packet */
    size_t total = net_buf_frags_len(rx_chain);
    process_packet(rx_chain, total);

    net_buf_unref(rx_chain);
    rx_chain = NULL;
}
```

#### Pattern 3: Protocol Layer Stripping

```c
/* Strip L2 header, return L3 payload */
struct net_buf *strip_l2_header(struct net_buf *pkt)
{
    /* Pull L2 header from first fragment */
    struct l2_header *l2 = net_buf_pull_mem(pkt, sizeof(*l2));

    /* If first fragment is now empty, remove it */
    if (pkt->len == 0 && pkt->frags) {
        pkt = net_buf_frag_del(NULL, pkt);
    }

    return pkt;
}
```

---

### Memory Considerations

- Each fragment consumes one `net_buf` from the pool
- Fragment chains share the reference count model — `unref` on head releases all
- Pool sizing: account for worst-case fragmentation (e.g., MTU-sized fragments for max packet)

```c
/* Example: Support 1500-byte packets with 256-byte fragments */
#define FRAG_SIZE 256
#define MAX_FRAGS ((1500 + FRAG_SIZE - 1) / FRAG_SIZE)  /* 6 */
#define POOL_COUNT (MAX_FRAGS * MAX_CONCURRENT_PACKETS)

NET_BUF_POOL_DEFINE(pkt_pool, POOL_COUNT, FRAG_SIZE, 0, NULL);
```

## Operations

The `net_buf` API provides four fundamental operations for manipulating buffer data. Understanding when to use each is key to efficient protocol encoding/decoding.

### Operation Summary

```
           ┌─────────────────────────────────────────────┐
           │              Buffer Storage                 │
           └─────────────────────────────────────────────┘
           ^                                             ^
           │                                             │
        __buf                                       __buf + size

           ┌───────────┬─────────────────┬───────────────┐
           │ headroom  │      data       │   tailroom    │
           └───────────┴─────────────────┴───────────────┘
                       ^                 ^
                       │                 │
                     data            data + len

           ◄── PUSH    ─── ADD ──►
           ◄── PULL    ─── REMOVE ──►
```

| Operation | Affects | Data Ptr | Length | Requirement |
|-----------|---------|----------|--------|-------------|
| **Add** | Tail | Unchanged | Increases | Tailroom available |
| **Remove** | Tail | Unchanged | Decreases | Data exists |
| **Push** | Head | Moves left | Increases | Headroom available |
| **Pull** | Head | Moves right | Decreases | Data exists |

---

### Encoding Data (Building Packets)

Use **Add** to append payload, **Push** to prepend headers.

#### Example: Building a Protocol Packet

```c
/* Reserve headroom for headers */
struct net_buf *buf = net_buf_alloc(&pool, K_FOREVER);
net_buf_reserve(buf, HEADER_SIZE);

/* 1. Add payload to tail */
net_buf_add_mem(buf, payload_data, payload_len);

/* 2. Push header to head (after payload is ready) */
net_buf_push_u8(buf, PACKET_TYPE);
net_buf_push_le16(buf, total_length);
net_buf_push_be32(buf, sequence_number);
```

#### Why Push After Add?

Headers often contain fields (like length) that depend on payload size. Build payload first, then prepend the header with the calculated length.

---

### Decoding Data (Parsing Packets)

Use **Pull** to consume headers, **Remove** to strip trailers.

#### Example: Parsing a Protocol Packet

```c
/* Incoming buffer contains: [header][payload][crc] */

/* 1. Pull header fields from front */
uint8_t type = net_buf_pull_u8(buf);
uint16_t length = net_buf_pull_le16(buf);
uint32_t seq = net_buf_pull_be32(buf);

/* 2. Remove CRC from tail (if present) */
uint16_t crc = net_buf_remove_le16(buf);

/* 3. Remaining buf->data points to payload, buf->len is payload size */
process_payload(buf->data, buf->len);
```

---

### Endian-Aware Helpers

For multi-byte values, choose the correct endianness:

| Suffix | Meaning | Use Case |
|--------|---------|----------|
| `_le16`, `_le32`, `_le64` | Little-endian | Bluetooth, USB |
| `_be16`, `_be32`, `_be64` | Big-endian | Network protocols (IP, TCP) |

```c
/* Bluetooth (little-endian) */
net_buf_add_le16(buf, handle);
uint16_t handle = net_buf_pull_le16(buf);

/* Network (big-endian) */
net_buf_add_be32(buf, ip_addr);
uint32_t ip = net_buf_pull_be32(buf);
```

---

### Byte-by-Byte Protocol Parsing

For serial protocols (SLIP, HDLC), process one byte at a time:

```c
static int slip_process_byte(struct net_buf *buf, uint8_t c)
{
    switch (c) {
    case SLIP_END:
        return 1; /* Packet complete */
    case SLIP_ESC:
        /* Next byte is escaped */
        return -EAGAIN;
    default:
        if (net_buf_tailroom(buf) > 0) {
            net_buf_add_u8(buf, c);
            return 0;
        }
        return -ENOMEM;
    }
}
```

---

### Memory Copy Operations

#### Add with Memory Copy

```c
/* Copy data to tail */
void *net_buf_add_mem(struct net_buf *buf, const void *mem, size_t len);

/* Example: Add raw bytes */
uint8_t data[] = {0x01, 0x02, 0x03};
net_buf_add_mem(buf, data, sizeof(data));
```

#### Push with Memory Copy

```c
/* Copy data to head (prepend) */
void *net_buf_push_mem(struct net_buf *buf, const void *mem, size_t len);

/* Example: Prepend MAC header */
net_buf_push_mem(buf, mac_header, MAC_HEADER_LEN);
```

#### Pull with Memory Copy

```c
/* Consume and get pointer to data */
void *net_buf_pull_mem(struct net_buf *buf, size_t len);

/* Example: Extract and copy header */
struct my_header *hdr = net_buf_pull_mem(buf, sizeof(*hdr));
```

---

### Checking Available Space

Before operations, verify space is available:

```c
/* Before Add: check tailroom */
if (net_buf_tailroom(buf) >= len) {
    net_buf_add_mem(buf, data, len);
}

/* Before Push: check headroom */
if (net_buf_headroom(buf) >= HEADER_SIZE) {
    net_buf_push_le16(buf, value);
}

/* Before Pull/Remove: check data exists */
if (buf->len >= sizeof(uint32_t)) {
    uint32_t val = net_buf_pull_le32(buf);
}
```

---

### Common Patterns

#### Pattern 1: UART TX with snprintk

```c
struct net_buf *buf = net_buf_alloc(&tx_pool, K_FOREVER);

int len = snprintk(buf->data, net_buf_tailroom(buf),
                   "Message: %d\r\n", value);
net_buf_add(buf, len);  /* Update length after snprintk */

uart_tx(dev, buf->data, buf->len, SYS_FOREVER_US);
```

#### Pattern 2: Protocol Frame Builder

```c
struct net_buf *build_frame(uint8_t cmd, const uint8_t *payload, size_t len)
{
    struct net_buf *buf = net_buf_alloc(&pool, K_FOREVER);

    /* Reserve space for frame header */
    net_buf_reserve(buf, FRAME_HEADER_SIZE);

    /* Add payload */
    net_buf_add_mem(buf, payload, len);

    /* Add CRC at tail */
    uint16_t crc = calc_crc(buf->data, buf->len);
    net_buf_add_le16(buf, crc);

    /* Push header at front */
    net_buf_push_u8(buf, cmd);
    net_buf_push_le16(buf, buf->len);  /* Length includes CRC */
    net_buf_push_u8(buf, FRAME_START);

    return buf;
}
```

#### Pattern 3: Streaming Parser State Machine

```c
enum parse_state { WAIT_START, READ_LEN, READ_DATA, VERIFY_CRC };

struct parser {
    enum parse_state state;
    uint16_t expected_len;
    struct net_buf *buf;
};

void parse_byte(struct parser *p, uint8_t c)
{
    switch (p->state) {
    case WAIT_START:
        if (c == FRAME_START) {
            p->buf = net_buf_alloc(&pool, K_NO_WAIT);
            p->state = READ_LEN;
        }
        break;
    case READ_LEN:
        net_buf_add_u8(p->buf, c);
        if (p->buf->len == 2) {
            p->expected_len = net_buf_pull_le16(p->buf);
            p->state = READ_DATA;
        }
        break;
    case READ_DATA:
        net_buf_add_u8(p->buf, c);
        if (p->buf->len == p->expected_len) {
            p->state = VERIFY_CRC;
        }
        break;
    /* ... */
    }
}
```
