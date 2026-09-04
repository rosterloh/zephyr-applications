# State Machine Framework

## Overview

SMF is an application-agnostic framework for integrating state machines into Zephyr applications. Enable with `CONFIG_SMF=y`.

### Core Concepts

#### State Structure

Every state has three optional actions:
- **Entry**: Executes when entering the state
- **Run**: Executes on each `smf_run_state()` call
- **Exit**: Executes when leaving the state

```c
// User object MUST have smf_ctx as FIRST member
struct s_object {
    struct smf_ctx ctx;  // MUST be first
    /* application-specific data */
};
```

#### State Definition

```c
enum app_state { STATE_IDLE, STATE_ACTIVE, STATE_ERROR };

static const struct smf_state app_states[] = {
    [STATE_IDLE]   = SMF_CREATE_STATE(idle_entry, idle_run, idle_exit, NULL, NULL),
    [STATE_ACTIVE] = SMF_CREATE_STATE(active_entry, active_run, NULL, NULL, NULL),
    [STATE_ERROR]  = SMF_CREATE_STATE(NULL, error_run, error_exit, NULL, NULL),
};
```

**SMF_CREATE_STATE parameters**: `(entry, run, exit, parent, initial)`
- Set unused actions to `NULL`
- `parent`: For hierarchical states (requires `CONFIG_SMF_ANCESTOR_SUPPORT`)
- `initial`: Initial child state (requires `CONFIG_SMF_INITIAL_TRANSITION`)

#### Action Signatures

```c
// Entry and exit actions
static void state_entry(void *o) { /* ... */ }
static void state_exit(void *o) { /* ... */ }

// Run action - returns event handling result
static enum smf_state_result state_run(void *o) {
    struct s_object *s = (struct s_object *)o;
    // Process events, trigger transitions
    return SMF_EVENT_HANDLED;  // or SMF_EVENT_PROPAGATE for HSM
}
```

### Workflow

#### 1. Determine State Machine Type

| Type | Use Case | Kconfig |
|------|----------|---------|
| **Flat** | Simple sequential states, no nesting | `CONFIG_SMF=y` |
| **Hierarchical (HSM)** | Shared behavior, nested states | Add `CONFIG_SMF_ANCESTOR_SUPPORT=y` |
| **HSM + Initial Transitions** | Auto-transition to child states | Add `CONFIG_SMF_INITIAL_TRANSITION=y` |

**Step 1a**: For flat state machines, proceed to step 2.

**Step 1b**: For hierarchical state machines, read [#hierarchical](#hierarchical).

#### 2. Implementation Pattern

```c
#include <zephyr/smf.h>

static struct s_object s_obj;
static const struct smf_state app_states[];  // Forward declaration

// 1. Define state actions
static void idle_entry(void *o) { /* initialize */ }
static enum smf_state_result idle_run(void *o) {
    struct s_object *s = (struct s_object *)o;
    if (/* condition */) {
        smf_set_state(SMF_CTX(&s_obj), &app_states[STATE_ACTIVE]);
    }
    return SMF_EVENT_HANDLED;
}

// 2. Populate state table
static const struct smf_state app_states[] = {
    [STATE_IDLE] = SMF_CREATE_STATE(idle_entry, idle_run, NULL, NULL, NULL),
    // ... other states
};

// 3. Initialize and run
int main(void) {
    smf_set_initial(SMF_CTX(&s_obj), &app_states[STATE_IDLE]);

    while (1) {
        int32_t ret = smf_run_state(SMF_CTX(&s_obj));
        if (ret) {
            break;  // State machine terminated
        }
        k_msleep(100);
    }
}
```

#### 3. Event-Driven Pattern

For event-driven state machines, read [#patterns](#patterns).

#### 4. API Reference

For complete API signatures and Kconfig options, read [#api](#api).

### Critical Rules

1. **User object layout**: `struct smf_ctx` MUST be the first member
2. **SMF_CTX macro**: Always use `SMF_CTX(&user_obj)` for API calls
3. **Transition restrictions**:
   - Call `smf_set_state()` only from entry or run actions, NEVER from exit
   - Stop calling `smf_run_state()` when it returns non-zero
4. **HSM leaf states**: Without `CONFIG_SMF_INITIAL_TRANSITION`, always transition to leaf states, not parent states
5. **Termination**: Use `smf_set_terminate(ctx, error_code)` to stop the state machine

### Common Pitfalls

| Issue | Cause | Solution |
|-------|-------|----------|
| Crashes on state access | `smf_ctx` not first member | Ensure `struct smf_ctx ctx;` is first in user object |
| Transition ignored | Called from exit action | Move transition to run action |
| Parent entry/exit not called | Transition between siblings | This is correct UML behavior - LCA actions are skipped |
| HSM stuck in parent state | Missing initial transition | Enable `CONFIG_SMF_INITIAL_TRANSITION` or always set leaf states |

### Source Locations

| Description | Path |
|:---|:---|
| SMF Header | `<zephyr-ws>/deps/zephyr/include/zephyr/smf.h` |
| SMF Implementation | `<zephyr-ws>/deps/zephyr/lib/smf/smf.c` |
| SMF Documentation | `<zephyr-ws>/deps/zephyr/doc/services/smf/index.rst` |
| Calculator Sample | `<zephyr-ws>/deps/zephyr/samples/subsys/smf/smf_calculator` |
| HSM Sample (PSiCC2) | `<zephyr-ws>/deps/zephyr/samples/subsys/smf/hsm_psicc2` |

## Api

### Table of Contents

- [Kconfig Options](#kconfig-options)
- [Macros](#macros)
- [Types](#types)
- [Functions](#functions)

### Kconfig Options

| Option | Description |
|--------|-------------|
| `CONFIG_SMF` | Enable State Machine Framework |
| `CONFIG_SMF_ANCESTOR_SUPPORT` | Enable parent state support (hierarchical state machines) |
| `CONFIG_SMF_INITIAL_TRANSITION` | Enable initial transitions to child states (requires `SMF_ANCESTOR_SUPPORT`) |

### Macros

#### SMF_CREATE_STATE

Create a state definition.

```c
SMF_CREATE_STATE(entry, run, exit, parent, initial)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `entry` | `state_method` or `NULL` | Entry action function |
| `run` | `state_execution` or `NULL` | Run action function |
| `exit` | `state_method` or `NULL` | Exit action function |
| `parent` | `const struct smf_state *` or `NULL` | Parent state (requires `CONFIG_SMF_ANCESTOR_SUPPORT`) |
| `initial` | `const struct smf_state *` or `NULL` | Initial child state (requires `CONFIG_SMF_INITIAL_TRANSITION`) |

#### SMF_CTX

Cast user object to state machine context.

```c
SMF_CTX(o)
```

**Usage**: `SMF_CTX(&user_obj)` instead of `(struct smf_ctx *)&user_obj`

### Types

#### struct smf_state

```c
struct smf_state {
    const state_method entry;           // Entry action (optional)
    const state_execution run;          // Run action (optional)
    const state_method exit;            // Exit action (optional)
#ifdef CONFIG_SMF_ANCESTOR_SUPPORT
    const struct smf_state *parent;     // Parent state (optional)
#ifdef CONFIG_SMF_INITIAL_TRANSITION
    const struct smf_state *initial;    // Initial child state (optional)
#endif
#endif
};
```

#### struct smf_ctx

```c
struct smf_ctx {
    const struct smf_state *current;    // Current leaf state
    const struct smf_state *previous;   // Previous state
#ifdef CONFIG_SMF_ANCESTOR_SUPPORT
    const struct smf_state *executing;  // Currently executing state (may be parent)
#endif
    int32_t terminate_val;              // Termination value
    uint32_t internal;                  // Internal state tracking
};
```

#### enum smf_state_result

```c
enum smf_state_result {
    SMF_EVENT_HANDLED,    // Event was handled, don't propagate to parent
    SMF_EVENT_PROPAGATE,  // Propagate event to parent run action (HSM only)
};
```

#### Function Pointer Types

```c
typedef void (*state_method)(void *obj);              // Entry/exit signature
typedef enum smf_state_result (*state_execution)(void *obj);  // Run signature
```

### Functions

#### smf_set_initial

Initialize the state machine and set initial state.

```c
void smf_set_initial(struct smf_ctx *ctx, const struct smf_state *init_state);
```

| Parameter | Description |
|-----------|-------------|
| `ctx` | State machine context |
| `init_state` | Initial state (should be leaf state unless `CONFIG_SMF_INITIAL_TRANSITION` is enabled) |

**Behavior**:
- Sets `ctx->current` to `init_state`
- Sets `ctx->previous` to `NULL`
- Executes entry actions from topmost ancestor to `init_state`
- With `CONFIG_SMF_INITIAL_TRANSITION`: follows initial transitions to deepest child

#### smf_set_state

Transition to a new state.

```c
void smf_set_state(struct smf_ctx *ctx, const struct smf_state *new_state);
```

| Parameter | Description |
|-----------|-------------|
| `ctx` | State machine context |
| `new_state` | Target state (must not be `NULL`) |

**Behavior**:
- Executes exit actions from current state up to (not including) Least Common Ancestor (LCA)
- Executes entry actions from LCA down to `new_state`
- For self-transitions: both exit and entry are called
- With `CONFIG_SMF_INITIAL_TRANSITION`: follows initial transitions to deepest child

**Restrictions**:
- Call only from entry or run actions, NEVER from exit actions
- Calling from exit action logs error and is ignored

#### smf_run_state

Execute one iteration of the state machine.

```c
int32_t smf_run_state(struct smf_ctx *ctx);
```

| Parameter | Description |
|-----------|-------------|
| `ctx` | State machine context |

**Returns**:
- `0`: Continue running
- Non-zero: Termination value (stop calling `smf_run_state`)

**Behavior**:
- Executes current state's run action
- With `CONFIG_SMF_ANCESTOR_SUPPORT`: propagates to parent run actions if `SMF_EVENT_PROPAGATE` returned

#### smf_set_terminate

Request state machine termination.

```c
void smf_set_terminate(struct smf_ctx *ctx, int32_t val);
```

| Parameter | Description |
|-----------|-------------|
| `ctx` | State machine context |
| `val` | Non-zero termination value returned by `smf_run_state` |

**Usage**: Call from any action (entry, run, or exit).

#### smf_get_current_leaf_state

Get the current leaf state.

```c
static inline const struct smf_state *smf_get_current_leaf_state(const struct smf_ctx *const ctx);
```

**Returns**: `ctx->current` (the leaf state)

**Note**: May return parent state if HSM is malformed (missing initial transitions).

#### smf_get_current_executing_state

Get the state whose action is currently executing.

```c
static inline const struct smf_state *smf_get_current_executing_state(const struct smf_ctx *const ctx);
```

**Returns**:
- With `CONFIG_SMF_ANCESTOR_SUPPORT`: `ctx->executing` (may be parent)
- Without: `ctx->current`

### Header Location

```c
#include <zephyr/smf.h>
```

## Hierarchical

### Table of Contents

- [Overview](#overview)
- [Kconfig Setup](#kconfig-setup)
- [Parent-Child State Structure](#parent-child-state-structure)
- [Initial Transitions](#initial-transitions)
- [Action Execution Order](#action-execution-order)
- [Event Propagation](#event-propagation)
- [UML Compliance](#uml-compliance)
- [Complete Example](#complete-example)

### Overview

Hierarchical state machines enable:
- **Shared behavior**: Common entry/exit logic in parent states
- **Reduced duplication**: Child states inherit parent behavior
- **Event bubbling**: Unhandled events propagate to parent run actions

### Kconfig Setup

```
CONFIG_SMF=y
CONFIG_SMF_ANCESTOR_SUPPORT=y
# Optional: enable auto-transition to child states
CONFIG_SMF_INITIAL_TRANSITION=y
```

### Parent-Child State Structure

```c
enum states { PARENT, CHILD_A, CHILD_B, STANDALONE };

static const struct smf_state app_states[] = {
    // Parent state - no run action (children handle events)
    [PARENT]     = SMF_CREATE_STATE(parent_entry, NULL, parent_exit, NULL, NULL),
    // Children reference parent
    [CHILD_A]    = SMF_CREATE_STATE(NULL, child_a_run, NULL, &app_states[PARENT], NULL),
    [CHILD_B]    = SMF_CREATE_STATE(NULL, child_b_run, NULL, &app_states[PARENT], NULL),
    // State without parent
    [STANDALONE] = SMF_CREATE_STATE(NULL, standalone_run, NULL, NULL, NULL),
};
```

#### Multi-Level Nesting

```c
enum states { ROOT, LEVEL1, LEVEL2_A, LEVEL2_B };

static const struct smf_state app_states[] = {
    [ROOT]      = SMF_CREATE_STATE(root_entry, NULL, root_exit, NULL, NULL),
    [LEVEL1]    = SMF_CREATE_STATE(l1_entry, NULL, l1_exit, &app_states[ROOT], NULL),
    [LEVEL2_A]  = SMF_CREATE_STATE(NULL, l2a_run, NULL, &app_states[LEVEL1], NULL),
    [LEVEL2_B]  = SMF_CREATE_STATE(NULL, l2b_run, NULL, &app_states[LEVEL1], NULL),
};
```

### Initial Transitions

With `CONFIG_SMF_INITIAL_TRANSITION=y`, parent states can auto-transition to a child:

```c
// Forward declaration required for self-reference
static const struct smf_state app_states[];

static const struct smf_state app_states[] = {
    // PARENT has initial transition to CHILD_A
    [PARENT]  = SMF_CREATE_STATE(parent_entry, NULL, parent_exit, NULL, &app_states[CHILD_A]),
    [CHILD_A] = SMF_CREATE_STATE(NULL, child_a_run, NULL, &app_states[PARENT], NULL),
    [CHILD_B] = SMF_CREATE_STATE(NULL, child_b_run, NULL, &app_states[PARENT], NULL),
};

// Can now transition to PARENT - will auto-resolve to CHILD_A
smf_set_state(SMF_CTX(&obj), &app_states[PARENT]);
```

**Without initial transitions**: Always transition to leaf states directly.

### Action Execution Order

#### Entry Actions

Executed **parent-first** (top-down):

```
Transition to LEVEL2_A:
1. ROOT entry
2. LEVEL1 entry
3. LEVEL2_A entry
```

#### Exit Actions

Executed **child-first** (bottom-up):

```
Leaving LEVEL2_A to STANDALONE:
1. LEVEL2_A exit
2. LEVEL1 exit
3. ROOT exit
```

#### Run Actions

Executed **child-first**, propagation controlled by return value:

```c
static enum smf_state_result child_run(void *o) {
    if (handled_event) {
        return SMF_EVENT_HANDLED;    // Stop here
    }
    return SMF_EVENT_PROPAGATE;      // Let parent handle
}

static enum smf_state_result parent_run(void *o) {
    // Only called if child returned SMF_EVENT_PROPAGATE
    return SMF_EVENT_HANDLED;
}
```

#### Sibling Transitions (LCA Rule)

When transitioning between siblings with shared parent, the **Least Common Ancestor (LCA)** entry/exit actions are NOT executed:

```
Transition from CHILD_A to CHILD_B (both under PARENT):
1. CHILD_A exit
2. CHILD_B entry
// PARENT entry/exit are NOT called
```

### Event Propagation

The run action return value controls propagation:

| Return Value | Behavior |
|--------------|----------|
| `SMF_EVENT_HANDLED` | Stop propagation, don't call parent run |
| `SMF_EVENT_PROPAGATE` | Call parent's run action |

**Note**: Calling `smf_set_state()` in a run action always stops propagation.

#### Propagation Example

```c
static enum smf_state_result child_run(void *o) {
    struct s_object *s = (struct s_object *)o;

    if (s->event == EVENT_CHILD_SPECIFIC) {
        // Handle locally
        return SMF_EVENT_HANDLED;
    }
    // Let parent handle unknown events
    return SMF_EVENT_PROPAGATE;
}

static enum smf_state_result parent_run(void *o) {
    struct s_object *s = (struct s_object *)o;

    if (s->event == EVENT_COMMON) {
        // Handle common events for all children
        return SMF_EVENT_HANDLED;
    }
    return SMF_EVENT_PROPAGATE;  // Propagate to grandparent if exists
}
```

### UML Compliance

SMF follows UML hierarchical state machine rules with these exceptions:

1. **Transition actions**: Executed in source state context (before exit), not after exit
2. **External self-transitions**: Only to self, not to sub-states. Transition to child is treated as local
3. **Exit transitions**: Prohibited - `smf_set_state()` in exit action logs error and is ignored

**Supported pseudostates**:
- Initial Pseudostate (via `CONFIG_SMF_INITIAL_TRANSITION`)

**Unsupported pseudostates**:
- Terminate pseudostate (model with `smf_set_terminate()` in entry action)
- Orthogonal regions (model with separate `smf_run_state()` calls)

### Complete Example

Three-level HSM with shared behavior:

```c
#include <zephyr/smf.h>

enum states { ROOT, ACTIVE, IDLE, RUNNING, PAUSED };

struct s_object {
    struct smf_ctx ctx;
    uint32_t event;
    uint32_t counter;
};

static struct s_object obj;
static const struct smf_state states[];

// ROOT: Top-level, handles global events
static void root_entry(void *o) {
    struct s_object *s = (struct s_object *)o;
    s->counter = 0;
}

static enum smf_state_result root_run(void *o) {
    struct s_object *s = (struct s_object *)o;
    if (s->event == EVENT_RESET) {
        s->counter = 0;
        smf_set_state(SMF_CTX(&obj), &states[IDLE]);
    }
    return SMF_EVENT_HANDLED;
}

// ACTIVE: Shared behavior for RUNNING and PAUSED
static void active_entry(void *o) { /* common setup */ }
static void active_exit(void *o) { /* common cleanup */ }

// RUNNING: Leaf state
static enum smf_state_result running_run(void *o) {
    struct s_object *s = (struct s_object *)o;
    if (s->event == EVENT_PAUSE) {
        smf_set_state(SMF_CTX(&obj), &states[PAUSED]);
        return SMF_EVENT_HANDLED;
    }
    return SMF_EVENT_PROPAGATE;  // Let ROOT handle other events
}

// PAUSED: Leaf state
static enum smf_state_result paused_run(void *o) {
    struct s_object *s = (struct s_object *)o;
    if (s->event == EVENT_RESUME) {
        smf_set_state(SMF_CTX(&obj), &states[RUNNING]);
        return SMF_EVENT_HANDLED;
    }
    return SMF_EVENT_PROPAGATE;
}

// IDLE: Leaf state, not under ACTIVE
static enum smf_state_result idle_run(void *o) {
    struct s_object *s = (struct s_object *)o;
    if (s->event == EVENT_START) {
        smf_set_state(SMF_CTX(&obj), &states[RUNNING]);
        return SMF_EVENT_HANDLED;
    }
    return SMF_EVENT_PROPAGATE;
}

static const struct smf_state states[] = {
    [ROOT]    = SMF_CREATE_STATE(root_entry, root_run, NULL, NULL, NULL),
    [ACTIVE]  = SMF_CREATE_STATE(active_entry, NULL, active_exit, &states[ROOT], NULL),
    [IDLE]    = SMF_CREATE_STATE(NULL, idle_run, NULL, &states[ROOT], NULL),
    [RUNNING] = SMF_CREATE_STATE(NULL, running_run, NULL, &states[ACTIVE], NULL),
    [PAUSED]  = SMF_CREATE_STATE(NULL, paused_run, NULL, &states[ACTIVE], NULL),
};

int main(void) {
    smf_set_initial(SMF_CTX(&obj), &states[IDLE]);

    while (1) {
        obj.event = get_next_event();  // Application-specific
        if (smf_run_state(SMF_CTX(&obj))) {
            break;
        }
    }
    return 0;
}
```

**Transition behavior in this example**:

| From | To | Entry/Exit Sequence |
|------|-----|---------------------|
| IDLE | RUNNING | IDLE exit → ACTIVE entry → RUNNING entry |
| RUNNING | PAUSED | RUNNING exit → PAUSED entry (ACTIVE skipped - LCA) |
| PAUSED | IDLE | PAUSED exit → ACTIVE exit → IDLE entry |

## Patterns

### Table of Contents

- [Event-Driven Pattern](#event-driven-pattern)
- [Polling Pattern](#polling-pattern)
- [Timeout Pattern](#timeout-pattern)
- [Error Handling Pattern](#error-handling-pattern)
- [Multiple State Machines](#multiple-state-machines)
- [State Machine Termination](#state-machine-termination)

### Event-Driven Pattern

Use Zephyr `k_event` for event-driven state machines:

```c
#include <zephyr/kernel.h>
#include <zephyr/smf.h>

#define EVENT_BUTTON    BIT(0)
#define EVENT_TIMEOUT   BIT(1)
#define EVENT_DATA      BIT(2)

struct s_object {
    struct smf_ctx ctx;
    struct k_event smf_event;
    uint32_t events;
    /* application data */
};

static struct s_object obj;

// ISR or other context posts events
void button_isr(void) {
    k_event_post(&obj.smf_event, EVENT_BUTTON);
}

// Run action checks events
static enum smf_state_result state_run(void *o) {
    struct s_object *s = (struct s_object *)o;

    if (s->events & EVENT_BUTTON) {
        smf_set_state(SMF_CTX(&obj), &states[NEXT_STATE]);
    }
    return SMF_EVENT_HANDLED;
}

// Main loop waits for events
int main(void) {
    k_event_init(&obj.smf_event);
    smf_set_initial(SMF_CTX(&obj), &states[INITIAL]);

    while (1) {
        // Block until event occurs
        obj.events = k_event_wait(&obj.smf_event,
            EVENT_BUTTON | EVENT_TIMEOUT | EVENT_DATA,
            true,       // Clear events after wait
            K_FOREVER);

        if (smf_run_state(SMF_CTX(&obj))) {
            break;
        }
    }
    return 0;
}
```

### Polling Pattern

Simple periodic state machine execution:

```c
int main(void) {
    smf_set_initial(SMF_CTX(&obj), &states[INITIAL]);

    while (1) {
        if (smf_run_state(SMF_CTX(&obj))) {
            break;
        }
        k_msleep(100);  // Poll interval
    }
    return 0;
}
```

### Timeout Pattern

State-specific timeouts using `k_timer`:

```c
struct s_object {
    struct smf_ctx ctx;
    struct k_timer timeout;
    bool timeout_expired;
};

static void timeout_handler(struct k_timer *timer) {
    struct s_object *s = CONTAINER_OF(timer, struct s_object, timeout);
    s->timeout_expired = true;
}

static void waiting_entry(void *o) {
    struct s_object *s = (struct s_object *)o;
    s->timeout_expired = false;
    k_timer_start(&s->timeout, K_SECONDS(5), K_NO_WAIT);
}

static enum smf_state_result waiting_run(void *o) {
    struct s_object *s = (struct s_object *)o;

    if (s->timeout_expired) {
        smf_set_state(SMF_CTX(s), &states[TIMEOUT_STATE]);
    }
    return SMF_EVENT_HANDLED;
}

static void waiting_exit(void *o) {
    struct s_object *s = (struct s_object *)o;
    k_timer_stop(&s->timeout);  // Cancel if transitioning early
}

int main(void) {
    k_timer_init(&obj.timeout, timeout_handler, NULL);
    // ...
}
```

### Error Handling Pattern

Centralized error handling with termination:

```c
enum states { INIT, RUNNING, ERROR };

struct s_object {
    struct smf_ctx ctx;
    int error_code;
};

static enum smf_state_result running_run(void *o) {
    struct s_object *s = (struct s_object *)o;
    int ret = do_operation();

    if (ret < 0) {
        s->error_code = ret;
        smf_set_state(SMF_CTX(s), &states[ERROR]);
    }
    return SMF_EVENT_HANDLED;
}

static void error_entry(void *o) {
    struct s_object *s = (struct s_object *)o;
    LOG_ERR("State machine error: %d", s->error_code);

    // Terminate with error code
    smf_set_terminate(SMF_CTX(s), s->error_code);
}

static const struct smf_state states[] = {
    [INIT]    = SMF_CREATE_STATE(init_entry, init_run, NULL, NULL, NULL),
    [RUNNING] = SMF_CREATE_STATE(NULL, running_run, NULL, NULL, NULL),
    [ERROR]   = SMF_CREATE_STATE(error_entry, NULL, NULL, NULL, NULL),
};

int main(void) {
    smf_set_initial(SMF_CTX(&obj), &states[INIT]);

    while (1) {
        int32_t ret = smf_run_state(SMF_CTX(&obj));
        if (ret) {
            LOG_ERR("State machine terminated with: %d", ret);
            // Handle cleanup
            break;
        }
        k_msleep(100);
    }
    return 0;
}
```

### Multiple State Machines

Run independent state machines in parallel:

```c
// Separate state machines
static struct sm_input input_obj;
static struct sm_output output_obj;

static const struct smf_state input_states[] = { /* ... */ };
static const struct smf_state output_states[] = { /* ... */ };

int main(void) {
    smf_set_initial(SMF_CTX(&input_obj), &input_states[0]);
    smf_set_initial(SMF_CTX(&output_obj), &output_states[0]);

    while (1) {
        int32_t ret1 = smf_run_state(SMF_CTX(&input_obj));
        int32_t ret2 = smf_run_state(SMF_CTX(&output_obj));

        if (ret1 || ret2) {
            break;
        }
        k_msleep(10);
    }
    return 0;
}
```

#### Orthogonal Regions (UML)

Model UML orthogonal regions as separate state machines sharing a context:

```c
struct shared_context {
    struct smf_ctx region1_ctx;
    struct smf_ctx region2_ctx;
    uint32_t shared_data;
};

// Run both regions
smf_run_state(SMF_CTX(&ctx.region1_ctx));
smf_run_state(SMF_CTX(&ctx.region2_ctx));
```

### State Machine Termination

#### Clean Termination

```c
static enum smf_state_result shutdown_run(void *o) {
    // Cleanup complete
    smf_set_terminate(SMF_CTX(o), 0);  // Success
    return SMF_EVENT_HANDLED;
}
```

#### Error Termination

```c
static enum smf_state_result error_run(void *o) {
    smf_set_terminate(SMF_CTX(o), -EFAULT);  // Error code
    return SMF_EVENT_HANDLED;
}
```

#### Checking Termination

```c
while (1) {
    int32_t ret = smf_run_state(SMF_CTX(&obj));
    if (ret) {
        if (ret > 0) {
            LOG_INF("Clean shutdown");
        } else {
            LOG_ERR("Error: %d", ret);
        }
        break;
    }
}
```

### Thread-Based Pattern

Run state machine in dedicated thread:

```c
#define SM_STACK_SIZE 1024
#define SM_PRIORITY 5

K_THREAD_STACK_DEFINE(sm_stack, SM_STACK_SIZE);
static struct k_thread sm_thread;

static void sm_thread_entry(void *p1, void *p2, void *p3) {
    struct s_object *obj = (struct s_object *)p1;

    smf_set_initial(SMF_CTX(obj), &states[INITIAL]);

    while (1) {
        if (smf_run_state(SMF_CTX(obj))) {
            break;
        }
        k_msleep(10);
    }
}

void start_state_machine(void) {
    k_thread_create(&sm_thread, sm_stack, SM_STACK_SIZE,
        sm_thread_entry, &obj, NULL, NULL,
        SM_PRIORITY, 0, K_NO_WAIT);
}
```
