# Memory Management

## Overview

Zephyr provides multiple memory management mechanisms tailored for different embedded use cases. This skill helps select the right allocator, implement memory patterns correctly, and avoid common pitfalls.

### Workflow

#### 1. Allocator Selection

Determine requirements before choosing:

-   **Block size variability?** Fixed vs variable-size allocations
-   **Determinism needed?** Constant-time allocation requirements
-   **Fragmentation tolerance?** Long-running systems need fragmentation resistance
-   **Memory protection?** Userspace isolation requirements
-   **ISR context?** Some allocators cannot be used from ISRs

**Step 1:** Read [#comparison](#comparison) for the allocator decision matrix.

#### 2. Implementation

Once the allocator is selected, implement using the appropriate guide.

**Step 2:** Read the appropriate reference:

-   **Heaps**: [#heaps](#heaps) — Variable-size dynamic allocation (`k_heap`, `k_malloc`, `sys_heap`).
-   **Memory Slabs**: [#slabs](#slabs) — Fixed-size block allocation with zero fragmentation.
-   **Memory Blocks**: [#mem_blocks](#mem_blocks) — Multi-block allocator with external bookkeeping.
-   **Memory Domains**: [#domains](#domains) — Memory partitions for userspace thread isolation.
-   **Virtual Memory**: [#virtual](#virtual) — MMU-based memory mapping and demand paging.

#### 3. API & Configuration

For complete API signatures and Kconfig options.

**Step 3:** Read [#api](#api) for:

-   Complete API function signatures for all allocators.
-   Relevant Kconfig options.
-   Header file locations.

#### 4. Troubleshooting

Common memory management issues:

-   **Fragmentation**: Use slabs or mem_blocks for fixed-size allocations; prefer multiple purpose-specific heaps over one large heap.
-   **Stack overflow**: Enable `CONFIG_HW_STACK_PROTECTION`; size stacks appropriately with `CONFIG_*_STACK_SIZE`.
-   **ISR allocation failures**: Never block in ISRs; use `K_NO_WAIT` and handle allocation failures.
-   **Memory leaks**: Track allocations; use heap listeners (`CONFIG_HEAP_LISTENER`) for debugging.
-   **Userspace access violations**: Verify memory partitions are correctly configured and threads are assigned to the right domains.

### Source Locations

| Description | Path |
| :--- | :--- |
| **Memory Management Docs** | `<zephyr-ws>/deps/zephyr/doc/kernel/memory_management` |
| **Memory Domain Docs** | `<zephyr-ws>/deps/zephyr/doc/kernel/usermode/memory_domain.rst` |
| **Memory Services Docs** | `<zephyr-ws>/deps/zephyr/doc/services/mem_mgmt` |
| **Kernel Header** | `<zephyr-ws>/deps/zephyr/include/zephyr/kernel.h` |
| **Sys Heap Header** | `<zephyr-ws>/deps/zephyr/include/zephyr/sys/sys_heap.h` |
| **Mem Blocks Header** | `<zephyr-ws>/deps/zephyr/include/zephyr/sys/mem_blocks.h` |
| **Mem Domain Header** | `<zephyr-ws>/deps/zephyr/include/zephyr/app_memory/mem_domain.h` |
| **Heap Sample** | `<zephyr-ws>/deps/zephyr/samples/kernel/heap` |
| **Slab Sample** | `<zephyr-ws>/deps/zephyr/samples/kernel/mem_slab` |

*Note: `<zephyr-ws>` represents the root of the Zephyr workspace.*

## Api

### Contents

- [Heaps](#heaps)
- [Memory Slabs](#memory-slabs)
- [Memory Blocks](#memory-blocks)
- [Memory Domains](#memory-domains)
- [Virtual Memory](#virtual-memory)
- [Kconfig Options](#kconfig-options)

---

### Heaps

#### k_heap

```c
#include <zephyr/kernel.h>

// Static definition
K_HEAP_DEFINE(name, size)

// Runtime init
void k_heap_init(struct k_heap *heap, void *mem, size_t bytes);

// Allocation
void *k_heap_alloc(struct k_heap *heap, size_t bytes, k_timeout_t timeout);
void *k_heap_aligned_alloc(struct k_heap *heap, size_t align, size_t bytes,
                            k_timeout_t timeout);
void *k_heap_realloc(struct k_heap *heap, void *ptr, size_t bytes,
                      k_timeout_t timeout);

// Deallocation
void k_heap_free(struct k_heap *heap, void *mem);
```

#### System Heap (k_malloc)

```c
#include <zephyr/kernel.h>

void *k_malloc(size_t size);
void *k_calloc(size_t nmemb, size_t size);
void *k_aligned_alloc(size_t align, size_t size);
void *k_realloc(void *ptr, size_t size);
void k_free(void *ptr);
```

#### sys_heap

```c
#include <zephyr/sys/sys_heap.h>

void sys_heap_init(struct sys_heap *heap, void *mem, size_t bytes);
void *sys_heap_alloc(struct sys_heap *heap, size_t bytes);
void *sys_heap_aligned_alloc(struct sys_heap *heap, size_t align, size_t bytes);
void *sys_heap_realloc(struct sys_heap *heap, void *ptr, size_t bytes);
void sys_heap_free(struct sys_heap *heap, void *mem);
size_t sys_heap_usable_size(struct sys_heap *heap, void *ptr);
```

#### Multi-Heap

```c
#include <zephyr/sys/multi_heap.h>

void sys_multi_heap_init(struct sys_multi_heap *heap,
                          sys_multi_heap_fn_t choice_fn);
void sys_multi_heap_add_heap(struct sys_multi_heap *mheap,
                              struct sys_heap *heap, void *user_data);
void *sys_multi_heap_alloc(struct sys_multi_heap *mheap, void *cfg, size_t bytes);
void *sys_multi_heap_aligned_alloc(struct sys_multi_heap *mheap, void *cfg,
                                    size_t align, size_t bytes);
void *sys_multi_heap_realloc(struct sys_multi_heap *mheap, void *cfg,
                              void *ptr, size_t bytes);
void sys_multi_heap_free(struct sys_multi_heap *mheap, void *mem);
```

#### Shared Multi-Heap

```c
#include <zephyr/multi_heap/shared_multi_heap.h>

int shared_multi_heap_pool_init(void);
int shared_multi_heap_add(struct shared_multi_heap_region *region, void *user_data);
void *shared_multi_heap_alloc(enum shared_multi_heap_attr attr, size_t bytes);
void *shared_multi_heap_aligned_alloc(enum shared_multi_heap_attr attr,
                                       size_t align, size_t bytes);
void shared_multi_heap_free(void *block);
```

#### Memory Attribute Heap

```c
#include <zephyr/mem_mgmt/mem_attr_heap.h>

int mem_attr_heap_pool_init(void);
void *mem_attr_heap_alloc(uint32_t attr, size_t bytes);
void *mem_attr_heap_aligned_alloc(uint32_t attr, size_t align, size_t bytes);
void mem_attr_heap_free(void *block);
```

---

### Memory Slabs

```c
#include <zephyr/kernel.h>

// Static definition
K_MEM_SLAB_DEFINE(name, block_size, num_blocks, align)
K_MEM_SLAB_DEFINE_STATIC(name, block_size, num_blocks, align)

// Runtime init
int k_mem_slab_init(struct k_mem_slab *slab, void *buffer,
                     size_t block_size, uint32_t num_blocks);

// Allocation
int k_mem_slab_alloc(struct k_mem_slab *slab, void **mem, k_timeout_t timeout);

// Deallocation
void k_mem_slab_free(struct k_mem_slab *slab, void *mem);

// Status
uint32_t k_mem_slab_num_used_get(struct k_mem_slab *slab);
uint32_t k_mem_slab_num_free_get(struct k_mem_slab *slab);
uint32_t k_mem_slab_max_used_get(struct k_mem_slab *slab);  // Needs CONFIG
```

---

### Memory Blocks

```c
#include <zephyr/sys/mem_blocks.h>

// Static definition
SYS_MEM_BLOCKS_DEFINE(name, block_size, num_blocks, align)
SYS_MEM_BLOCKS_DEFINE_STATIC(name, block_size, num_blocks, align)
SYS_MEM_BLOCKS_DEFINE_WITH_EXT_BUF(name, block_size, num_blocks, buffer)

// Allocation
int sys_mem_blocks_alloc(sys_mem_blocks_t *mem_block, size_t count,
                          void **out_blocks);
int sys_mem_blocks_alloc_contiguous(sys_mem_blocks_t *mem_block, size_t count,
                                     void **out_block);

// Deallocation
int sys_mem_blocks_free(sys_mem_blocks_t *mem_block, size_t count,
                         void **in_blocks);
int sys_mem_blocks_free_contiguous(sys_mem_blocks_t *mem_block, void *block,
                                    size_t count);

// Multi-block group
void sys_multi_mem_blocks_init(struct sys_multi_mem_blocks *group,
                                sys_multi_mem_blocks_choice_fn_t choice_fn);
void sys_multi_mem_blocks_add_allocator(struct sys_multi_mem_blocks *group,
                                         sys_mem_blocks_t *alloc);
int sys_multi_mem_blocks_alloc(struct sys_multi_mem_blocks *group, void *cfg,
                                size_t count, void **out_blocks, size_t *blk_size);
int sys_multi_mem_blocks_free(struct sys_multi_mem_blocks *group, size_t count,
                               void **in_blocks);
```

---

### Memory Domains

```c
#include <zephyr/kernel.h>
#include <zephyr/app_memory/app_memdomain.h>

// Partition definition
K_MEM_PARTITION_DEFINE(name, start, size, attr)
K_APPMEM_PARTITION_DEFINE(name)

// Variable routing
K_APP_DMEM(partition) type var = init;  // Initialized data
K_APP_BMEM(partition) type var;          // BSS (zeroed)

// Domain management
int k_mem_domain_init(struct k_mem_domain *domain, uint8_t num_parts,
                       struct k_mem_partition *parts[]);
int k_mem_domain_add_partition(struct k_mem_domain *domain,
                                struct k_mem_partition *part);
int k_mem_domain_remove_partition(struct k_mem_domain *domain,
                                   struct k_mem_partition *part);
int k_mem_domain_add_thread(struct k_mem_domain *domain, k_tid_t thread);

// Thread resource pool
void k_thread_heap_assign(struct k_thread *thread, struct k_heap *heap);
void k_thread_system_pool_assign(struct k_thread *thread);
```

---

### Virtual Memory

```c
#include <zephyr/kernel.h>

// Memory mapping
void *k_mem_map(size_t size, uint32_t flags);
void k_mem_unmap(void *addr, size_t size);
void *k_mem_map_phys_bare(uint8_t *phys, size_t size, uint32_t flags);

// Demand paging
void k_mem_page_in(void *addr, size_t size);
void k_mem_page_out(void *addr, size_t size);
void k_mem_pin(void *addr, size_t size);
void k_mem_unpin(void *addr, size_t size);

// Statistics
void k_mem_paging_stats_get(struct k_mem_paging_stats *stats);
void k_mem_paging_thread_stats_get(k_tid_t thread,
                                    struct k_mem_paging_stats *stats);
```

---

### Kconfig Options

#### System Heap

| Option | Description | Default |
|--------|-------------|---------|
| `CONFIG_HEAP_MEM_POOL_SIZE` | System heap size (0 disables) | 0 |
| `CONFIG_HEAP_MEM_POOL_IGNORE_MIN` | Ignore minimum size requirements | n |

#### Memory Slabs

| Option | Description | Default |
|--------|-------------|---------|
| `CONFIG_MEM_SLAB_TRACE_MAX_UTILIZATION` | Track peak usage | n |

#### Heap Debugging

| Option | Description | Default |
|--------|-------------|---------|
| `CONFIG_HEAP_LISTENER` | Enable heap allocation callbacks | n |
| `CONFIG_SYS_HEAP_RUNTIME_STATS` | Enable runtime statistics | n |
| `CONFIG_SYS_HEAP_ALLOC_LOOPS` | Allocation search iterations | 3 |

#### Userspace & Memory Protection

| Option | Description | Default |
|--------|-------------|---------|
| `CONFIG_USERSPACE` | Enable userspace support | n |
| `CONFIG_MAX_DOMAIN_PARTITIONS` | Max partitions per domain | 8 |
| `CONFIG_MEM_DOMAIN_ISOLATED_STACKS` | Isolate stacks between threads | y (if supported) |
| `CONFIG_HW_STACK_PROTECTION` | Hardware stack overflow detection | n |

#### Virtual Memory

| Option | Description | Default |
|--------|-------------|---------|
| `CONFIG_MMU` | Enable MMU support | n |
| `CONFIG_MMU_PAGE_SIZE` | Page size in bytes | 4096 |
| `CONFIG_KERNEL_VM_BASE` | Virtual address space base | arch-specific |
| `CONFIG_KERNEL_VM_SIZE` | Virtual address space size | 8MB |
| `CONFIG_KERNEL_DIRECT_MAP` | Allow 1:1 physical mappings | n |

#### Demand Paging

| Option | Description | Default |
|--------|-------------|---------|
| `CONFIG_DEMAND_PAGING` | Enable demand paging | n |
| `CONFIG_DEMAND_PAGING_ALLOW_IRQ` | Allow page faults in ISR | n |
| `CONFIG_DEMAND_PAGING_EVICTION_NRU` | NRU eviction algorithm | n |
| `CONFIG_DEMAND_PAGING_EVICTION_LRU` | LRU eviction algorithm | n |
| `CONFIG_DEMAND_PAGING_THREAD_STATS` | Per-thread paging stats | n |

#### Memory Attributes

| Option | Description | Default |
|--------|-------------|---------|
| `CONFIG_MEM_ATTR` | Enable memory attributes helper | n |
| `CONFIG_MEM_ATTR_HEAP` | Enable attribute-based heaps | n |

---

### Header Locations

| API | Header |
|-----|--------|
| k_heap, k_malloc, k_mem_slab | `<zephyr/kernel.h>` |
| sys_heap | `<zephyr/sys/sys_heap.h>` |
| sys_multi_heap | `<zephyr/sys/multi_heap.h>` |
| sys_mem_blocks | `<zephyr/sys/mem_blocks.h>` |
| heap_listener | `<zephyr/sys/heap_listener.h>` |
| k_mem_domain | `<zephyr/kernel.h>` |
| K_APP_DMEM, K_APPMEM_PARTITION_DEFINE | `<zephyr/app_memory/app_memdomain.h>` |
| partitions (z_libc_partition, etc.) | `<zephyr/app_memory/partitions.h>` |
| mem_attr_heap | `<zephyr/mem_mgmt/mem_attr_heap.h>` |
| shared_multi_heap | `<zephyr/multi_heap/shared_multi_heap.h>` |

## Comparison

### Decision Matrix

| Feature | k_heap | k_malloc | k_mem_slab | sys_mem_blocks | Memory Domain |
|---------|--------|----------|------------|----------------|---------------|
| **Block Size** | Variable | Variable | Fixed | Fixed | N/A (protection) |
| **Fragmentation** | Possible | Possible | None | None | N/A |
| **Allocation Time** | O(1) | O(1) | O(1) | O(n) blocks | N/A |
| **ISR Safe (K_NO_WAIT)** | Yes | No | Yes | Yes | N/A |
| **Blocking Wait** | Yes | No | Yes | No | N/A |
| **Multiple Instances** | Yes | No (single) | Yes | Yes | Yes |
| **External Bookkeeping** | No | No | No | Yes | N/A |
| **Synchronized** | Yes | Yes | Yes | No | N/A |
| **Best For** | General dynamic | Simple malloc | Protocol buffers | DMA scatter-gather | Userspace isolation |

### Decision Flowchart

```
Need memory allocation?
│
├─ Fixed-size blocks?
│   ├─ Yes → Need external bookkeeping (power-down memory)?
│   │         ├─ Yes → sys_mem_blocks
│   │         └─ No  → k_mem_slab
│   │
│   └─ No → Variable-size blocks
│           ├─ Need multiple separate heaps? → k_heap
│           └─ Single global heap sufficient? → k_malloc/k_free
│
├─ Memory protection needed?
│   └─ Yes → Memory Domains + Partitions (see domains.md)
│
└─ MMU available & virtual memory needed?
    └─ Yes → k_mem_map / Demand Paging (see virtual.md)
```

### When to Use Each

#### k_heap (Recommended Default)
- General-purpose dynamic allocation
- Variable-size allocations
- Need blocking with timeout
- Multiple isolated heaps for different subsystems

#### k_malloc / k_free
- Simple malloc-like interface
- Single system-wide heap is acceptable
- Cannot wait for memory (returns NULL immediately if unavailable)

#### k_mem_slab
- Fixed-size allocations (network packets, sensor samples)
- Zero fragmentation guarantee
- High-frequency alloc/free cycles
- Need blocking wait for available blocks

#### sys_mem_blocks
- Fixed-size block allocation
- Need to allocate multiple blocks atomically
- External bookkeeping (buffer can be in power-down region)
- DMA scatter-gather operations with non-contiguous blocks

#### Memory Domains
- Userspace thread isolation
- MPU/MMU-based memory protection
- Shared memory between specific thread groups

### Common Patterns

#### Pattern: Per-Subsystem Heaps
Avoid one large system heap. Create dedicated heaps:

```c
K_HEAP_DEFINE(network_heap, 4096);
K_HEAP_DEFINE(sensor_heap, 2048);

// Allocate from specific heap
void *pkt = k_heap_alloc(&network_heap, 256, K_NO_WAIT);
```

#### Pattern: Message Buffer Pool
Use slabs for fixed-size message buffers:

```c
K_MEM_SLAB_DEFINE(msg_pool, sizeof(struct msg), 10, 4);

struct msg *m;
k_mem_slab_alloc(&msg_pool, (void **)&m, K_FOREVER);
// Use message
k_mem_slab_free(&msg_pool, m);
```

#### Pattern: DMA Buffer Allocation
Use attribute heaps for DMA-capable memory:

```c
// Allocate from DMA-capable region (requires devicetree setup)
void *dma_buf = mem_attr_heap_alloc(DT_MEM_SW_ALLOC_DMA, 512);
```

## Domains

Memory domains provide memory protection for userspace threads using MPU or MMU hardware:
- Group memory partitions accessible to threads
- Isolate threads from each other and kernel
- Share specific memory regions between thread groups

Requires: `CONFIG_USERSPACE=y`

### Concepts

#### Memory Partition
A contiguous memory region with defined access attributes (read, write, execute).

#### Memory Domain
A collection of memory partitions. Threads in the same domain share access to those partitions.

#### Default Behavior
- All threads start in `k_mem_domain_default`
- Threads always have access to their own stack
- Kernel memory is never accessible from userspace

### Partition Definition

#### Manual Partition

```c
uint8_t __aligned(32) my_buffer[1024];

K_MEM_PARTITION_DEFINE(my_partition, my_buffer, sizeof(my_buffer),
                        K_MEM_PARTITION_P_RW_U_RW);
```

#### Automatic Partition (Build System)

```c
#include <zephyr/app_memory/app_memdomain.h>

// Declare partition (no base/size—computed by linker)
K_APPMEM_PARTITION_DEFINE(app_partition);

// Route variables to partition
K_APP_DMEM(app_partition) int initialized_var = 42;
K_APP_BMEM(app_partition) int bss_var;  // zeroed at boot
```

### Domain Creation

```c
struct k_mem_domain my_domain;

// Empty domain
k_mem_domain_init(&my_domain, 0, NULL);

// Domain with initial partitions
struct k_mem_partition *parts[] = { &part1, &part2 };
k_mem_domain_init(&my_domain, ARRAY_SIZE(parts), parts);
```

### Managing Partitions

```c
// Add partition to domain
k_mem_domain_add_partition(&my_domain, &my_partition);

// Remove partition from domain
k_mem_domain_remove_partition(&my_domain, &my_partition);
```

### Thread Assignment

```c
// Assign thread to domain
k_mem_domain_add_thread(&my_domain, my_thread);

// Child threads inherit parent's domain
```

### Partition Attributes

Common attributes (architecture-specific availability):

```c
K_MEM_PARTITION_P_RW_U_RW   // Privileged RW, User RW (most common)
K_MEM_PARTITION_P_RW_U_RO   // Privileged RW, User RO
K_MEM_PARTITION_P_RW_U_NA   // Privileged RW, User No Access
K_MEM_PARTITION_P_RO_U_RO   // Both RO
```

### Pre-defined Partitions

```c
// C library globals (required for libc usage)
extern struct k_mem_partition z_libc_partition;

// System malloc pool
extern struct k_mem_partition z_malloc_partition;

// Include library-specific partitions
#include <zephyr/app_memory/partitions.h>
```

### Complete Example

```c
#include <zephyr/kernel.h>
#include <zephyr/app_memory/app_memdomain.h>

// Application memory partition
K_APPMEM_PARTITION_DEFINE(app_part);
K_APP_DMEM(app_part) struct app_data shared_data = {0};

// Shared buffer
uint8_t __aligned(32) shared_buf[256];
K_MEM_PARTITION_DEFINE(shared_part, shared_buf, sizeof(shared_buf),
                        K_MEM_PARTITION_P_RW_U_RW);

// Domain for worker threads
struct k_mem_domain worker_domain;

K_THREAD_STACK_DEFINE(worker_stack, 1024);
struct k_thread worker_thread;

void worker_entry(void *p1, void *p2, void *p3) {
    // Can access shared_buf and app_part variables
    shared_data.counter++;
}

void setup_workers(void) {
    // Create domain with partitions
    struct k_mem_partition *parts[] = { &app_part, &shared_part };
    k_mem_domain_init(&worker_domain, ARRAY_SIZE(parts), parts);

    // Create userspace thread
    k_thread_create(&worker_thread, worker_stack,
                    K_THREAD_STACK_SIZEOF(worker_stack),
                    worker_entry, NULL, NULL, NULL,
                    WORKER_PRIORITY, K_USER, K_NO_WAIT);

    // Assign to domain
    k_mem_domain_add_thread(&worker_domain, &worker_thread);
}
```

### Thread Resource Pools

Userspace threads need heap for some kernel operations:

```c
K_HEAP_DEFINE(worker_heap, 4096);

void setup_thread(void) {
    // Assign heap for kernel allocations on behalf of thread
    k_thread_heap_assign(&worker_thread, &worker_heap);

    // Or use system heap
    k_thread_system_pool_assign(&worker_thread);
}
```

### MPU/MMU Considerations

- Maximum partitions limited by MPU region count
- Partitions must meet alignment requirements (power of 2 on many MPUs)
- Overlapping partitions within a domain not allowed
- Same partition can exist in multiple domains

### Stack Isolation

By default, user threads can access stacks of other threads in same domain. For stricter isolation:

```kconfig
CONFIG_MEM_DOMAIN_ISOLATED_STACKS=y  # If supported by arch
```

### Kconfig

```kconfig
CONFIG_USERSPACE=y
CONFIG_MAX_DOMAIN_PARTITIONS=8  # Limit partitions per domain
```

### Common Pitfalls

1. **MPU region limit**: Too many partitions exceeds hardware capability.

2. **Alignment violations**: Partitions must meet MPU alignment requirements.

3. **Forgetting libc partition**: Userspace threads using libc need `z_libc_partition`.

4. **Kernel memory exposure**: Never put kernel data in user-accessible partitions.

5. **Missing resource pool**: Some syscalls need thread heap assignment.

## Heaps

Zephyr provides three heap abstractions for variable-size dynamic memory allocation:
- **k_heap**: Kernel-synchronized heap with blocking support
- **sys_heap**: Low-level unsynchronized heap
- **System Heap**: Global malloc-like interface (`k_malloc`/`k_free`)

### k_heap (Recommended)

Thread-safe heap with blocking allocation support.

#### Definition

```c
// Static definition
K_HEAP_DEFINE(my_heap, 4096);

// Runtime initialization
struct k_heap my_heap;
uint8_t __aligned(8) heap_buffer[4096];
k_heap_init(&my_heap, heap_buffer, sizeof(heap_buffer));
```

#### Allocation

```c
// Non-blocking (ISR-safe)
void *ptr = k_heap_alloc(&my_heap, 256, K_NO_WAIT);
if (ptr == NULL) {
    // Handle allocation failure
}

// Blocking with timeout
void *ptr = k_heap_alloc(&my_heap, 256, K_MSEC(100));

// Blocking forever
void *ptr = k_heap_alloc(&my_heap, 256, K_FOREVER);

// Aligned allocation
void *ptr = k_heap_aligned_alloc(&my_heap, 64, 256, K_NO_WAIT);
```

#### Deallocation

```c
k_heap_free(&my_heap, ptr);
```

### sys_heap (Low-Level)

Unsynchronized heap for custom synchronization or single-threaded contexts.

**Critical:** Caller must ensure serialization—concurrent access causes corruption.

#### Usage

```c
#include <zephyr/sys/sys_heap.h>

struct sys_heap my_sys_heap;
uint8_t __aligned(8) buffer[4096];

// Initialize
sys_heap_init(&my_sys_heap, buffer, sizeof(buffer));

// Allocate (no synchronization!)
void *ptr = sys_heap_alloc(&my_sys_heap, 256);
void *aligned_ptr = sys_heap_aligned_alloc(&my_sys_heap, 64, 256);

// Free
sys_heap_free(&my_sys_heap, ptr);
```

#### Reallocation

```c
void *new_ptr = sys_heap_realloc(&my_sys_heap, old_ptr, new_size);
```

### System Heap (k_malloc)

Global heap for simple malloc-style allocation. Configure size via Kconfig.

#### Kconfig

```kconfig
CONFIG_HEAP_MEM_POOL_SIZE=8192
```

#### Usage

```c
// Allocate (non-blocking, returns NULL on failure)
char *buf = k_malloc(200);
if (buf != NULL) {
    memset(buf, 0, 200);
}

// Free
k_free(buf);

// Calloc (zeroed memory)
char *buf = k_calloc(10, sizeof(struct my_struct));

// Aligned allocation
void *ptr = k_aligned_alloc(64, 256);
```

**Note:** `k_malloc` cannot block—it returns NULL immediately if memory is unavailable.

### Multi-Heap

Manage multiple discontiguous memory regions as a unified allocator.

#### Setup

```c
#include <zephyr/sys/multi_heap.h>

struct sys_multi_heap multi_heap;
struct sys_heap heap1, heap2;

// Choice function selects which heap to use
sys_heap_t *my_choice(struct sys_multi_heap *mheap, void *cfg,
                       size_t align, size_t size) {
    // Custom logic based on cfg, size, etc.
    return &heap1;
}

// Initialize
sys_multi_heap_init(&multi_heap, my_choice);
sys_multi_heap_add_heap(&multi_heap, &heap1, NULL);
sys_multi_heap_add_heap(&multi_heap, &heap2, NULL);
```

#### Allocation

```c
void *ptr = sys_multi_heap_alloc(&multi_heap, cfg, size);
void *ptr = sys_multi_heap_aligned_alloc(&multi_heap, cfg, align, size);
sys_multi_heap_free(&multi_heap, ptr);
```

### Shared Multi-Heap

Attribute-based allocation from devicetree-defined memory regions.

#### Devicetree

```dts
mem_cacheable: memory@10000000 {
    compatible = "mmio-sram";
    reg = <0x10000000 0x1000>;
    zephyr,memory-attr = <( DT_MEM_CACHEABLE | DT_MEM_SW_ALLOC_CACHE )>;
};

mem_dma: memory@20000000 {
    compatible = "mmio-sram";
    reg = <0x20000000 0x1000>;
    zephyr,memory-attr = <( DT_MEM_DMA | DT_MEM_SW_ALLOC_DMA )>;
};
```

#### Usage

```c
#include <zephyr/mem_mgmt/mem_attr_heap.h>

// Initialize at boot
mem_attr_heap_pool_init();

// Allocate by attribute
void *cached = mem_attr_heap_alloc(DT_MEM_SW_ALLOC_CACHE, 256);
void *dma_buf = mem_attr_heap_alloc(DT_MEM_SW_ALLOC_DMA, 512);

// Free
mem_attr_heap_free(cached);
```

### Heap Listener (Debugging)

Monitor heap allocations for debugging memory leaks.

#### Kconfig

```kconfig
CONFIG_HEAP_LISTENER=y
```

#### Usage

```c
#include <zephyr/sys/heap_listener.h>

void alloc_cb(uintptr_t heap_id, void *mem, size_t bytes) {
    printk("Alloc %zu bytes at %p\n", bytes, mem);
}

void free_cb(uintptr_t heap_id, void *mem, size_t bytes) {
    printk("Free %zu bytes at %p\n", bytes, mem);
}

HEAP_LISTENER_ALLOC_DEFINE(my_listener, HEAP_ID_FROM_POINTER(&my_heap),
                            alloc_cb);
HEAP_LISTENER_FREE_DEFINE(my_free_listener, HEAP_ID_FROM_POINTER(&my_heap),
                           free_cb);

// Register
heap_listener_register(&my_listener);
heap_listener_register(&my_free_listener);
```

### Common Pitfalls

1. **Using k_malloc in ISR**: Never works—always returns NULL. Use `k_heap_alloc` with `K_NO_WAIT`.

2. **Forgetting to check NULL**: Always check allocation result before use.

3. **sys_heap without synchronization**: Concurrent access corrupts the heap.

4. **Single large system heap**: Causes fragmentation. Use multiple purpose-specific heaps.

5. **Blocking forever on low memory**: `K_FOREVER` can deadlock if no memory is ever freed.

## Mem Blocks

`sys_mem_blocks` is a fixed-size block allocator with:
- External bookkeeping (bitmap stored separately from buffer)
- Multi-block atomic allocation
- Support for non-contiguous block results (scatter-gather)
- Buffer can reside in power-down memory regions

Best for: DMA scatter-gather, power-managed memory regions, multi-block allocations.

### Key Differences from k_mem_slab

| Feature | k_mem_slab | sys_mem_blocks |
|---------|------------|----------------|
| Bookkeeping | In-buffer (free list) | External (bitmap) |
| Multi-block alloc | No | Yes |
| Contiguous result | N/A | Not guaranteed |
| Blocking wait | Yes | No |
| Synchronized | Yes | No |
| Power-down buffer | No | Yes |

### Definition

#### Static (Compile-Time)

```c
#include <zephyr/sys/mem_blocks.h>

// SYS_MEM_BLOCKS_DEFINE(name, block_size, num_blocks, align)
SYS_MEM_BLOCKS_DEFINE(my_blocks, 64, 16, 4);

// Private scope
SYS_MEM_BLOCKS_DEFINE_STATIC(my_blocks, 64, 16, 4);
```

#### With External Buffer

```c
uint8_t __aligned(4) backing_buffer[64 * 16];
SYS_MEM_BLOCKS_DEFINE_WITH_EXT_BUF(my_blocks, 64, 16, backing_buffer);
```

### Single Block Allocation

```c
void *block;

int ret = sys_mem_blocks_alloc(&my_blocks, 1, &block);
if (ret == 0) {
    // Use block
}

// Free single block
sys_mem_blocks_free(&my_blocks, 1, &block);
```

### Multi-Block Allocation

Allocate multiple blocks atomically. Blocks may not be contiguous.

```c
uintptr_t blocks[4];

int ret = sys_mem_blocks_alloc(&my_blocks, 4, blocks);
if (ret == 0) {
    // blocks[0..3] contain addresses of allocated blocks
    // These may NOT be contiguous in memory
}

// Free all blocks
sys_mem_blocks_free(&my_blocks, 4, blocks);
```

### Contiguous Allocation

When contiguous memory is required:

```c
void *ptr;
int ret = sys_mem_blocks_alloc_contiguous(&my_blocks, 4, &ptr);
if (ret == 0) {
    // ptr points to 4 contiguous blocks
}
```

### Multi Memory Blocks Group

Manage multiple allocators as a group with custom selection logic.

#### Setup

```c
sys_mem_blocks_t *choice_fn(struct sys_multi_mem_blocks *group, void *cfg) {
    uintptr_t attr = (uintptr_t)cfg;
    // Select allocator based on attributes
    if (attr & ATTR_DMA) {
        return &dma_blocks;
    }
    return &normal_blocks;
}

SYS_MEM_BLOCKS_DEFINE(normal_blocks, 64, 16, 4);
SYS_MEM_BLOCKS_DEFINE(dma_blocks, 64, 8, 4);

struct sys_multi_mem_blocks block_group;

void init(void) {
    sys_multi_mem_blocks_init(&block_group, choice_fn);
    sys_multi_mem_blocks_add_allocator(&block_group, &normal_blocks);
    sys_multi_mem_blocks_add_allocator(&block_group, &dma_blocks);
}
```

#### Allocation

```c
uintptr_t blocks[2];
size_t block_size;

int ret = sys_multi_mem_blocks_alloc(&block_group,
                                      UINT_TO_POINTER(ATTR_DMA),
                                      2, blocks, &block_size);

// Free (no config needed—auto-detected)
sys_multi_mem_blocks_free(&block_group, 2, blocks);
```

### DMA Scatter-Gather Example

```c
SYS_MEM_BLOCKS_DEFINE(dma_pool, DMA_BLOCK_SIZE, 32, 4);

struct dma_block_config dma_cfg[MAX_DMA_BLOCKS];
uintptr_t blocks[MAX_DMA_BLOCKS];

int setup_dma_transfer(size_t total_size) {
    int num_blocks = DIV_ROUND_UP(total_size, DMA_BLOCK_SIZE);

    int ret = sys_mem_blocks_alloc(&dma_pool, num_blocks, blocks);
    if (ret != 0) {
        return ret;
    }

    // Configure scatter-gather DMA
    for (int i = 0; i < num_blocks; i++) {
        dma_cfg[i].source_address = blocks[i];
        dma_cfg[i].block_size = DMA_BLOCK_SIZE;
        if (i < num_blocks - 1) {
            dma_cfg[i].next_block = &dma_cfg[i + 1];
        }
    }

    return 0;
}
```

### Power-Down Memory Example

Buffer in external RAM that can be powered down:

```c
// Buffer in special memory section
uint8_t __aligned(4) __attribute__((section(".ext_ram")))
    ext_buffer[64 * 32];

// Bookkeeping stays in always-on RAM
SYS_MEM_BLOCKS_DEFINE_WITH_EXT_BUF(ext_blocks, 64, 32, ext_buffer);

// Before power-down: free all blocks or track allocations
// After power-up: buffer contents are lost but allocator state preserved
```

### Synchronization

**sys_mem_blocks is NOT synchronized**. Wrap with mutex if needed:

```c
K_MUTEX_DEFINE(blocks_mutex);

void *safe_alloc(void) {
    uintptr_t block;
    k_mutex_lock(&blocks_mutex, K_FOREVER);
    int ret = sys_mem_blocks_alloc(&my_blocks, 1, &block);
    k_mutex_unlock(&blocks_mutex);
    return (ret == 0) ? (void *)block : NULL;
}
```

### Common Pitfalls

1. **Assuming contiguity**: Multi-block alloc does NOT guarantee contiguous blocks.

2. **No synchronization**: Must protect concurrent access manually.

3. **Blocking expectation**: No blocking wait—returns error immediately if unavailable.

4. **Wrong block count to free**: Must free exact number of blocks allocated.

## Slabs

Memory slabs provide fixed-size block allocation with:
- Zero fragmentation
- O(1) constant-time allocation
- Blocking wait support
- ISR-safe allocation with `K_NO_WAIT`

Best for: protocol buffers, sensor samples, message queues, any fixed-size data structures.

### Definition

#### Static (Compile-Time)

```c
// K_MEM_SLAB_DEFINE(name, block_size, num_blocks, align)
K_MEM_SLAB_DEFINE(my_slab, 64, 10, 4);

// Private scope
K_MEM_SLAB_DEFINE_STATIC(my_slab, 64, 10, 4);
```

#### Runtime Initialization

```c
struct k_mem_slab my_slab;
char __aligned(4) my_buffer[10 * 64];  // num_blocks * block_size

k_mem_slab_init(&my_slab, my_buffer, 64, 10);
```

### Allocation

```c
void *block;

// Non-blocking (ISR-safe)
if (k_mem_slab_alloc(&my_slab, &block, K_NO_WAIT) == 0) {
    // Use block
} else {
    // No blocks available
}

// Blocking with timeout
int ret = k_mem_slab_alloc(&my_slab, &block, K_MSEC(100));
if (ret == 0) {
    memset(block, 0, 64);
}

// Blocking forever
k_mem_slab_alloc(&my_slab, &block, K_FOREVER);
```

### Deallocation

```c
k_mem_slab_free(&my_slab, block);
```

### Status Queries

```c
// Number of currently used blocks
uint32_t used = k_mem_slab_num_used_get(&my_slab);

// Number of free blocks
uint32_t free = k_mem_slab_num_free_get(&my_slab);

// Maximum utilization (requires CONFIG_MEM_SLAB_TRACE_MAX_UTILIZATION)
uint32_t max_used = k_mem_slab_max_used_get(&my_slab);
```

### Practical Example: Message Pool

```c
struct sensor_msg {
    uint32_t timestamp;
    int16_t  data[3];
    uint8_t  sensor_id;
};

K_MEM_SLAB_DEFINE(sensor_msg_pool, sizeof(struct sensor_msg), 20, 4);

// Producer (can be ISR)
void sensor_isr(void *arg) {
    struct sensor_msg *msg;

    if (k_mem_slab_alloc(&sensor_msg_pool, (void **)&msg, K_NO_WAIT) == 0) {
        msg->timestamp = k_uptime_get_32();
        msg->data[0] = read_sensor_x();
        msg->data[1] = read_sensor_y();
        msg->data[2] = read_sensor_z();
        msg->sensor_id = SENSOR_ACCEL;

        k_msgq_put(&sensor_msgq, &msg, K_NO_WAIT);
    }
}

// Consumer
void sensor_thread(void) {
    struct sensor_msg *msg;

    while (1) {
        k_msgq_get(&sensor_msgq, &msg, K_FOREVER);
        process_sensor_data(msg);
        k_mem_slab_free(&sensor_msg_pool, msg);
    }
}
```

### Alignment Requirements

- Block size must be at least 4 bytes (for internal linkage)
- Block size must be multiple of alignment
- Buffer must be aligned to specified alignment

```c
// 32-byte aligned blocks of 128 bytes each
K_MEM_SLAB_DEFINE(aligned_slab, 128, 8, 32);
```

### Internal Operation

Slabs use a free list stored in the first 4 bytes of each unused block:
- No separate metadata structure
- All bookkeeping within the buffer itself
- Constant-time alloc/free via list head manipulation

### Kconfig

```kconfig
# Enable max utilization tracking
CONFIG_MEM_SLAB_TRACE_MAX_UTILIZATION=y
```

### Common Pitfalls

1. **Block size too small**: Must be at least 4 bytes and multiple of alignment.

2. **Misaligned buffer**: Runtime init requires properly aligned buffer.

3. **Blocking in ISR**: Use `K_NO_WAIT` in interrupt context.

4. **Wrong pointer to free**: Passing incorrect address corrupts the free list.

5. **Using freed block**: Block contents may be modified after free (free list linkage).

## Virtual

Virtual memory in Zephyr provides:
- Address space isolation
- Fine-grained access control
- Memory-mapped regions
- Optional demand paging for memory overcommit

Requires: MMU hardware and `CONFIG_MMU=y`

### Virtual Memory Basics

#### Key Concepts

- **Virtual Address**: Address used by software
- **Physical Address**: Actual RAM location
- **Page**: Smallest mappable unit (typically 4KB)
- **Page Table**: Maps virtual to physical addresses

#### Default Mapping

By default, Zephyr uses 1:1 identity mapping:
- Virtual address == Physical address
- Simplifies embedded development
- Kernel image mapped at boot

### Memory Mapping

#### Map Anonymous Memory

Allocate virtual memory backed by physical RAM:

```c
#include <zephyr/kernel.h>

// Map 4 pages of read-write memory
void *vaddr = k_mem_map(4 * CONFIG_MMU_PAGE_SIZE,
                         K_MEM_PERM_RW);

if (vaddr != NULL) {
    // Use memory
    memset(vaddr, 0, 4 * CONFIG_MMU_PAGE_SIZE);
}

// Unmap when done
k_mem_unmap(vaddr, 4 * CONFIG_MMU_PAGE_SIZE);
```

#### Map Physical Address

Map specific physical memory (MMIO, shared memory):

```c
// Map device registers
void *regs = k_mem_map_phys_bare((uint8_t *)0x40000000,
                                  0x1000,
                                  K_MEM_PERM_RW | K_MEM_CACHE_NONE);
```

#### Permission Flags

```c
K_MEM_PERM_RW     // Read-write
K_MEM_PERM_RO     // Read-only
K_MEM_PERM_EXEC   // Executable
K_MEM_PERM_USER   // User-mode accessible
K_MEM_CACHE_NONE  // Uncached (for MMIO)
K_MEM_CACHE_WB    // Write-back cache
K_MEM_CACHE_WT    // Write-through cache
```

### Kconfig

```kconfig
# Enable MMU support
CONFIG_MMU=y

# Page size (default 4096)
CONFIG_MMU_PAGE_SIZE=4096

# Virtual address space base
CONFIG_KERNEL_VM_BASE=0x80000000

# Virtual address space size
CONFIG_KERNEL_VM_SIZE=0x800000  # 8MB

# Allow direct physical mappings
CONFIG_KERNEL_DIRECT_MAP=y
```

### Demand Paging

Allows virtual memory larger than physical RAM by paging to backing store.

#### Enable

```kconfig
CONFIG_DEMAND_PAGING=y
CONFIG_DEMAND_PAGING_ALLOW_IRQ=y  # Allow paging in ISR context
```

#### Manual Page Control

```c
// Page in memory proactively
k_mem_page_in(addr, size);

// Page out memory (hint to free physical pages)
k_mem_page_out(addr, size);

// Pin memory (prevent paging out)
k_mem_pin(addr, size);

// Unpin memory
k_mem_unpin(addr, size);
```

#### Statistics

```c
#include <zephyr/kernel.h>

struct k_mem_paging_stats stats;
k_mem_paging_stats_get(&stats);

printk("Page faults: %llu\n", stats.pagefaults);
printk("Pages evicted: %llu\n", stats.eviction);
```

#### Eviction Algorithms

```kconfig
# NRU: Not-Recently-Used (simple)
CONFIG_DEMAND_PAGING_EVICTION_NRU=y

# LRU: Least-Recently-Used (recommended for production)
CONFIG_DEMAND_PAGING_EVICTION_LRU=y
```

#### Backing Store

Storage for paged-out memory:

```kconfig
# RAM-based backing store (for testing)
CONFIG_BACKING_STORE_RAM=y
CONFIG_BACKING_STORE_RAM_PAGES=64
```

Custom backing store requires implementing:
- `k_mem_paging_backing_store_init()`
- `k_mem_paging_backing_store_page_in()`
- `k_mem_paging_backing_store_page_out()`

### Memory Map Layout

```
+--------------+ <- K_MEM_VIRT_RAM_START
| Reserved     |
+--------------+ <- K_MEM_KERNEL_VIRT_START
| Kernel Image |
| .text        |
| .rodata      |
| .data/.bss   |
+--------------+ <- K_MEM_VM_FREE_START
| Available    |
| Virtual      |
| Address      |
| Space        |
|..............| <- grows downward
| Mappings     |
+--------------+
| Reserved     |
+--------------+ <- K_MEM_VIRT_RAM_END
```

### Boot-Time Memory Regions

Set up in device tree or architecture code:

```dts
/ {
    sram0: memory@20000000 {
        compatible = "mmio-sram";
        reg = <0x20000000 0x40000>;
    };
};
```

### Section Permissions

At boot, Zephyr sets up:
- `.text`: Read-only, executable, user-accessible
- `.rodata`: Read-only, non-executable, user-accessible
- `.data`, `.bss`: Read-write, non-executable, kernel-only

### Practical Example: Large Buffer

```c
// Allocate large buffer that may exceed physical RAM
void *large_buf = k_mem_map(1024 * 1024,  // 1MB
                             K_MEM_PERM_RW);

if (large_buf == NULL) {
    LOG_ERR("Failed to map virtual memory");
    return -ENOMEM;
}

// Pin critical sections
k_mem_pin(large_buf, 4096);  // Keep first page always resident

// Use buffer - page faults will bring in pages as needed
process_data(large_buf);

// Unmap when done
k_mem_unmap(large_buf, 1024 * 1024);
```

### Common Pitfalls

1. **No MMU hardware**: Virtual memory requires MMU; most MCUs only have MPU.

2. **Page fault in ISR**: Default config disallows; enable `CONFIG_DEMAND_PAGING_ALLOW_IRQ` carefully.

3. **Backing store latency**: Page faults are slow; pin performance-critical memory.

4. **Memory overcommit**: Don't allocate more virtual memory than physical + backing store.

5. **Alignment**: All addresses and sizes must be page-aligned.
