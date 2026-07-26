# CBOR Serialization (zcbor)

## Overview

Zephyr integrates the [zcbor](https://github.com/NordicSemiconductor/zcbor)
library as a west module for [CBOR](https://www.rfc-editor.org/rfc/rfc8949)
(Concise Binary Object Representation) encoding and decoding. CBOR is the
binary serialization used by CoAP, LwM2M, SUIT manifests, and the SMP
management protocol over BLE/UART.

Two ways to use zcbor:

1. **CDDL code generation** (recommended for non-trivial schemas) — write
   a [CDDL](https://datatracker.ietf.org/doc/html/rfc8610) schema, run
   `zcbor code`, get type-safe `struct`s and `encode`/`decode` functions.
2. **Manual API** — call `zcbor_*` primitives directly against a
   `zcbor_state_t` state machine. All functions return `bool`; chain
   with `&&`.

### Quick Start: Manual Encode

```c
#include <zcbor_encode.h>

uint8_t buf[64];
ZCBOR_STATE_E(zse, 0, buf, sizeof(buf), 1);  /* 0 backups, 1 top-level elem */

bool ok = zcbor_map_start_encode(zse, 2)
       && zcbor_tstr_put_lit(zse, "temp")
       && zcbor_int32_put(zse, 22)
       && zcbor_tstr_put_lit(zse, "unit")
       && zcbor_tstr_put_lit(zse, "C")
       && zcbor_map_end_encode(zse, 2);

size_t len = zse->payload - buf;  /* bytes written */
```

### Quick Start: Manual Decode

```c
#include <zcbor_decode.h>

ZCBOR_STATE_D(zsd, 0, buf, buf_len, 1, 0);  /* 0 backups, 1 elem, 0 flags */

int32_t temp;
struct zcbor_string unit;

bool ok = zcbor_map_start_decode(zsd)
       && zcbor_tstr_expect_lit(zsd, "temp")
       && zcbor_int32_decode(zsd, &temp)
       && zcbor_tstr_expect_lit(zsd, "unit")
       && zcbor_tstr_decode(zsd, &unit)
       && zcbor_map_end_decode(zsd);
```

`struct zcbor_string { const uint8_t *value; size_t len; }` points
into the original payload buffer — **zero-copy**, but valid only while
that buffer lives.

### Important Notes

1. **Errors are sticky-ish.** Without `CONFIG_ZCBOR_STOP_ON_ERROR`,
   subsequent calls keep executing after a failure; chain with `&&`
   and check `zcbor_peek_error(state)` at the end.
2. **State macros lie down the stack.** `ZCBOR_STATE_E/D` declares a
   local array — don't return a pointer to it from a function.
3. **`size_hint` only matters under `CONFIG_ZCBOR_CANONICAL`.** Pass
   `0` for `map/list_start_encode` if you don't know or don't care.
4. **Backups consume RAM.** Unordered-map search needs
   `num_backups >= 1`; nested bstr decode needs another.
5. **`_put` takes a value, `_encode` takes a pointer.** The pointer
   variants are needed by `zcbor_multi_encode`.

## CDDL Code Generation

### 1. Write a schema

CDDL has two container styles — pick based on whether field order
matters on the wire:

- **Group/tuple** `(...)` — ordered, no keys on wire (compact).
- **Map** `{...}` — key/value, order-independent (flexible).

```cddl
; my-api.cddl

cmd_get_status = 1
cmd_set_config = 2
command_id /= cmd_get_status
command_id /= cmd_set_config

status_idle   = 0
status_active = 1
status_error  = 2
device_status /= status_idle
device_status /= status_active
device_status /= status_error

set_config_params = (
    threshold: uint .size 2,
    enabled:   bool,
)

status_response = {
    status_code: device_status,
    uptime_ms:   uint,
    ? error_msg: tstr,
}

My_Command = (
    command_id: command_id,
    ? params:   set_config_params,
)
```

### 2. Generate C code

```bash
pip install zcbor

zcbor code --decode --encode \
  --short-names \
  -c my-api.cddl \
  -t My_Command status_response \
  --output-c src/cbor/my-api.c \
  --output-h include/cbor/my-api.h \
  --output-h-types include/cbor/my-api_types.h \
  --include-prefix "cbor/" \
  --default-max-qty 3
```

| Flag                     | Purpose                                              |
|--------------------------|------------------------------------------------------|
| `--decode` / `--encode`  | Generate decode and/or encode functions              |
| `-t <types>`             | Root types to expose as entry-point functions        |
| `--short-names`          | Shorten generated symbol names                       |
| `--output-c/h/h-types`   | Output paths (`.c` is auto-split _encode/_decode)    |
| `--include-prefix`       | Prefix on `#include` paths inside generated `.c`     |
| `--default-max-qty N`    | Max array/list entries (sizes generated arrays)      |
| `--file-header <file>`   | Prepend custom license header                        |

Generated `.c` files split into `<name>_encode.c` / `<name>_decode.c`
automatically. **Never hand-edit generated files** — regenerate from
the `.cddl`.

### 3. Wire regeneration into CMake

```cmake
set(zcbor_command
    zcbor code --decode --encode --short-names
    -c ${CMAKE_CURRENT_LIST_DIR}/my-api.cddl
    -t My_Command status_response
    --output-c       ${CMAKE_CURRENT_LIST_DIR}/src/cbor/my-api.c
    --output-h       ${CMAKE_CURRENT_LIST_DIR}/include/cbor/my-api.h
    --output-h-types ${CMAKE_CURRENT_LIST_DIR}/include/cbor/my-api_types.h
    --include-prefix "cbor/"
    --default-max-qty 3
)

add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_LIST_DIR}/src/cbor/my-api_decode.c
        ${CMAKE_CURRENT_LIST_DIR}/src/cbor/my-api_encode.c
        ${CMAKE_CURRENT_LIST_DIR}/include/cbor/my-api_decode.h
        ${CMAKE_CURRENT_LIST_DIR}/include/cbor/my-api_encode.h
        ${CMAKE_CURRENT_LIST_DIR}/include/cbor/my-api_types.h
    COMMAND ${zcbor_command}
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/my-api.cddl
    COMMENT "Regenerating CBOR code from CDDL schema"
)
```

### 4. Use the generated API

Naming conventions:

- **Structs**: `struct <TypeName>`
- **Union choice enum**: `<union_name>_<value_name>_m_c`
- **Union choice field**: `<union_name>_choice`
- **Optional fields**: `bool <field>_present` next to the field
- **Repeated fields**: `<field>_m[N]` + `size_t <field>_m_count`
- **Single-field groups** are flattened into their parent struct

```c
#include "cbor/my-api_decode.h"
#include "cbor/my-api_encode.h"
#include "cbor/my-api_types.h"

/* --- Decode --- */
struct My_Command cmd = {0};
size_t consumed;

int err = cbor_decode_My_Command(buf, buf_len, &cmd, &consumed);
if (err != ZCBOR_SUCCESS) {
    return err;
}

switch (cmd.command_id_choice) {
case command_id_cmd_get_status_m_c:
    break;
case command_id_cmd_set_config_m_c:
    if (cmd.params_present) {
        uint32_t threshold = cmd.params.threshold;
        bool     enabled   = cmd.params.enabled;
    }
    break;
}

/* --- Encode --- */
struct status_response resp = {
    .status_code.device_status_choice = device_status_status_active_m_c,
    .uptime_ms = k_uptime_get_32(),
    .error_msg_present = false,
};
uint8_t out[64];
size_t  out_len;
cbor_encode_status_response(out, sizeof(out), &resp, &out_len);
```

### When to use codegen vs. manual

| Scenario                                            | Approach                  |
|-----------------------------------------------------|---------------------------|
| Complex/nested schema, many message types           | Codegen                   |
| Schema evolves over time                            | Codegen                   |
| Simple one-off encode/decode, few fields            | Manual                    |
| CoAP/LwM2M/SUIT (schemas already published)         | Codegen                   |

## References

- **API Details**: [#api](#api)
- **Kconfig Options**: [#kconfig](#kconfig)
- **Source Locations**: [#locations](#locations)
- **Patterns**: [#patterns](#patterns)

### Related Skills

- **zephyr-connectivity**: `references/sockets.md` (CoAP/UDP transport),
  `references/bluetooth-le.md` (SMP over BLE)
- This skill, `references/json.md` — text-based alternative
- This skill, `references/protobuf.md` — schema-driven alternative

## Api

### Headers

```c
#include <zcbor_encode.h>   /* encoding */
#include <zcbor_decode.h>   /* decoding */
#include <zcbor_common.h>   /* shared types (zcbor_state_t, zcbor_string) */
```

### Type Quick Reference

| C type             | Encode (`_put`)                        | Decode                                  |
|--------------------|----------------------------------------|-----------------------------------------|
| `int8/16/32/64_t`  | `zcbor_int32_put(s, v)`                | `zcbor_int32_decode(s, &r)`             |
| `uint8/16/32/64_t` | `zcbor_uint32_put(s, v)`               | `zcbor_uint32_decode(s, &r)`            |
| `bool`             | `zcbor_bool_put(s, v)`                 | `zcbor_bool_decode(s, &r)`              |
| `float`            | `zcbor_float32_put(s, v)`              | `zcbor_float32_decode(s, &r)`           |
| `double`           | `zcbor_float64_put(s, v)`              | `zcbor_float64_decode(s, &r)`           |
| text string        | `zcbor_tstr_put_lit(s, "x")`           | `zcbor_tstr_decode(s, &zcbor_string)`   |
| byte string        | `zcbor_bstr_encode_ptr(s, p, n)`       | `zcbor_bstr_decode(s, &zcbor_string)`   |
| nil                | `zcbor_nil_put(s, NULL)`               | `zcbor_nil_expect(s, NULL)`             |
| list/array         | `zcbor_list_start/end_encode`          | `zcbor_list_start/end_decode`           |
| map                | `zcbor_map_start/end_encode`           | `zcbor_map_start/end_decode`            |

### State Types

```c
struct zcbor_string {
    const uint8_t *value;   /* points into payload buffer (zero-copy) */
    size_t len;
};

struct zcbor_string_fragment {
    struct zcbor_string fragment;
    size_t offset;          /* offset in the full string */
    size_t total_len;       /* ZCBOR_STRING_FRAGMENT_UNKNOWN_LENGTH if unknown */
};
```

### State Initialization

```c
/* Encoder */
ZCBOR_STATE_E(name, num_backups, payload, payload_size, elem_count);

/* Decoder */
ZCBOR_STATE_D(name, num_backups, payload, payload_size, elem_count, n_flags);

/* Low-level init (rarely needed) */
void zcbor_new_encode_state(zcbor_state_t *state_array, size_t n_states,
                            uint8_t *payload, size_t payload_len,
                            size_t elem_count);
void zcbor_new_decode_state(zcbor_state_t *state_array, size_t n_states,
                            const uint8_t *payload, size_t payload_len,
                            size_t elem_count, uint8_t *elem_state,
                            size_t elem_state_bytes);
```

### Encoding Primitives

All return `bool` — `true` on success.

Only 32- and 64-bit integer widths have dedicated functions. For 8- and
16-bit values use the generic size-taking variants (or just widen to 32-bit —
CBOR encodes the smallest wire representation either way).

```c
/* _put takes a value; _encode takes a pointer (for zcbor_multi_encode) */
bool zcbor_int32_put(zcbor_state_t *s, int32_t v);
bool zcbor_int64_put(zcbor_state_t *s, int64_t v);
bool zcbor_uint32_put(zcbor_state_t *s, uint32_t v);
bool zcbor_uint64_put(zcbor_state_t *s, uint64_t v);
bool zcbor_size_put(zcbor_state_t *s, size_t v);

/* Any width, passed by pointer + sizeof — this is how you do int8/int16 */
bool zcbor_int_encode(zcbor_state_t *s, const void *input_int, size_t int_size);
bool zcbor_uint_encode(zcbor_state_t *s, const void *input_uint, size_t uint_size);

bool zcbor_bool_put(zcbor_state_t *s, bool v);
bool zcbor_nil_put(zcbor_state_t *s, const void *unused);
bool zcbor_undefined_put(zcbor_state_t *s, const void *unused);
bool zcbor_float16_put(zcbor_state_t *s, float v);
bool zcbor_float32_put(zcbor_state_t *s, float v);
bool zcbor_float64_put(zcbor_state_t *s, double v);
bool zcbor_tag_put(zcbor_state_t *s, uint32_t tag);

bool zcbor_int32_encode(zcbor_state_t *s, const int32_t *v);
bool zcbor_uint32_encode(zcbor_state_t *s, const uint32_t *v);
bool zcbor_bool_encode(zcbor_state_t *s, const bool *v);
bool zcbor_float32_encode(zcbor_state_t *s, const float *v);
bool zcbor_float64_encode(zcbor_state_t *s, const double *v);
```

### Encoding Strings

```c
bool zcbor_bstr_encode(zcbor_state_t *s, const struct zcbor_string *in);
bool zcbor_tstr_encode(zcbor_state_t *s, const struct zcbor_string *in);
bool zcbor_bstr_encode_ptr(zcbor_state_t *s, const char *str, size_t len);
bool zcbor_tstr_encode_ptr(zcbor_state_t *s, const char *str, size_t len);
bool zcbor_bstr_put_term(zcbor_state_t *s, char const *str, size_t maxlen);
bool zcbor_tstr_put_term(zcbor_state_t *s, char const *str, size_t maxlen);

/* Convenience macros */
#define zcbor_bstr_put_lit(state, str)   /* sizeof(str)-1 */
#define zcbor_tstr_put_lit(state, str)   /* sizeof(str)-1 */
#define zcbor_bstr_put_arr(state, str)   /* sizeof(str) */
#define zcbor_tstr_put_arr(state, str)   /* sizeof(str) */
```

### Encoding Lists, Maps, Nested

```c
bool zcbor_list_start_encode(zcbor_state_t *s, size_t size_hint);
bool zcbor_list_end_encode(zcbor_state_t *s, size_t size_hint);
bool zcbor_map_start_encode(zcbor_state_t *s, size_t size_hint);
bool zcbor_map_end_encode(zcbor_state_t *s, size_t size_hint);
bool zcbor_list_map_end_force_encode(zcbor_state_t *s);

/* Nested CBOR inside a bstr */
bool zcbor_bstr_start_encode(zcbor_state_t *s);
bool zcbor_bstr_end_encode(zcbor_state_t *s, struct zcbor_string *result);

/* Multi-encode helper */
bool zcbor_multi_encode(size_t num_encode, zcbor_encoder_t encoder,
                        zcbor_state_t *s, const void *input,
                        size_t result_len);
bool zcbor_multi_encode_minmax(size_t min_encode, size_t max_encode,
                               const size_t *num_encode,
                               zcbor_encoder_t encoder, zcbor_state_t *s,
                               const void *input, size_t input_len);
```

`size_hint` is used only with `CONFIG_ZCBOR_CANONICAL`. Pass `0` if
unused. Use `ZCBOR_CAST_FP(fn)` to cast function pointers for
`zcbor_multi_encode`.

### Decoding Primitives

Same width rule as encoding: 32/64-bit only, plus generic size-taking forms.

```c
bool zcbor_int32_decode(zcbor_state_t *s, int32_t *r);
bool zcbor_int64_decode(zcbor_state_t *s, int64_t *r);
bool zcbor_uint32_decode(zcbor_state_t *s, uint32_t *r);
bool zcbor_uint64_decode(zcbor_state_t *s, uint64_t *r);
bool zcbor_size_decode(zcbor_state_t *s, size_t *r);

/* Any width — errors if the decoded value doesn't fit in result_size */
bool zcbor_int_decode(zcbor_state_t *s, void *result, size_t result_size);
bool zcbor_uint_decode(zcbor_state_t *s, void *result, size_t result_size);

bool zcbor_bool_decode(zcbor_state_t *s, bool *r);
bool zcbor_float16_decode(zcbor_state_t *s, float *r);
bool zcbor_float32_decode(zcbor_state_t *s, float *r);
bool zcbor_float64_decode(zcbor_state_t *s, double *r);
bool zcbor_float_decode(zcbor_state_t *s, double *r);  /* any float width */
bool zcbor_tag_decode(zcbor_state_t *s, uint32_t *r);
bool zcbor_bstr_decode(zcbor_state_t *s, struct zcbor_string *r);
bool zcbor_tstr_decode(zcbor_state_t *s, struct zcbor_string *r);
```

### Expect (decode + validate value)

```c
bool zcbor_int32_expect(zcbor_state_t *s, int32_t expected);
bool zcbor_uint32_expect(zcbor_state_t *s, uint32_t expected);
bool zcbor_bool_expect(zcbor_state_t *s, bool expected);
bool zcbor_nil_expect(zcbor_state_t *s, void *unused);
bool zcbor_undefined_expect(zcbor_state_t *s, void *unused);
bool zcbor_tstr_expect(zcbor_state_t *s, struct zcbor_string *expected);
bool zcbor_bstr_expect(zcbor_state_t *s, struct zcbor_string *expected);
bool zcbor_tag_expect(zcbor_state_t *s, uint32_t expected);

#define zcbor_tstr_expect_lit(state, str)
#define zcbor_bstr_expect_lit(state, str)
#define zcbor_tstr_expect_term(state, str, maxlen)
```

### Decoding Lists, Maps, Nested

```c
bool zcbor_list_start_decode(zcbor_state_t *s);
bool zcbor_list_end_decode(zcbor_state_t *s);
bool zcbor_map_start_decode(zcbor_state_t *s);
bool zcbor_map_end_decode(zcbor_state_t *s);

/* Unordered map search */
bool zcbor_unordered_map_start_decode(zcbor_state_t *s);
bool zcbor_unordered_map_end_decode(zcbor_state_t *s);
bool zcbor_unordered_map_search(zcbor_decoder_t key_decoder,
                                zcbor_state_t *s, void *key_result);

bool zcbor_search_key_tstr_lit(zcbor_state_t *s, const char *str);
bool zcbor_search_key_bstr_lit(zcbor_state_t *s, const char *str);
bool zcbor_search_key_tstr_ptr(zcbor_state_t *s, char const *ptr, size_t len);
bool zcbor_search_key_tstr_term(zcbor_state_t *s, char const *str, size_t maxlen);

/* Nested CBOR inside a bstr */
bool zcbor_bstr_start_decode(zcbor_state_t *s, struct zcbor_string *r);
bool zcbor_bstr_end_decode(zcbor_state_t *s);

/* Skip one element of any type */
bool zcbor_any_skip(zcbor_state_t *s, void *unused);

/* Multi-decode helper */
bool zcbor_multi_decode(size_t min_decode, size_t max_decode,
                        size_t *num_decode, zcbor_decoder_t decoder,
                        zcbor_state_t *s, void *result, size_t result_len);
```

Unordered-map search requires `num_backups >= 1` in `ZCBOR_STATE_D`,
and `n_flags >=` max number of elements in any unordered map.

### Error Handling

```c
int zcbor_peek_error(const zcbor_state_t *state);  /* returns ZCBOR_ERR_* */
```

| Code                            | Meaning                                  |
|---------------------------------|------------------------------------------|
| `ZCBOR_ERR_NO_PAYLOAD`          | buffer exhausted                         |
| `ZCBOR_ERR_WRONG_TYPE`          | type mismatch                            |
| `ZCBOR_ERR_HIGH_ELEM_COUNT`     | too many elements in list/map            |
| `ZCBOR_ERR_NOT_AT_END`          | list/map didn't consume all elements     |
| `ZCBOR_ERR_VALUE_NOT_FOUND`     | `_expect()` value mismatch               |
| `ZCBOR_ERR_PAYLOAD_NOT_CONSUMED`| trailing bytes after decoding            |

### Encoded Size

```c
/* After encoding, bytes written = payload_ptr - start_of_buffer */
size_t len = zse->payload - buf;

/* No pre-computation helper exists; for canonical mode, encode into a
   scratch buffer and read the resulting length. */
```

## Kconfig

### Required

```kconfig
CONFIG_ZCBOR=y
```

Enables the zcbor library — pulls in the zcbor west module.

### Optional

```kconfig
# Canonical encoding (definite-length maps/arrays, sorted map keys).
# Uses memmove() to rewrite headers — bigger code/RAM.
CONFIG_ZCBOR_CANONICAL=y

# Make stop-on-error *available* — you must ALSO set
# state->constant_state->stop_on_error at runtime for it to take effect.
CONFIG_ZCBOR_STOP_ON_ERROR=y

# Verbose messages via printf()
CONFIG_ZCBOR_VERBOSE=y

# Default max length for zcbor_tstr_put_term() (discouraged; pass a length)
CONFIG_ZCBOR_MAX_STR_LEN=256
```

`CONFIG_ZCBOR_ASSERT` and `CONFIG_ZCBOR_BIG_ENDIAN` exist but are
`def_bool` — they track `CONFIG_ASSERT` / `CONFIG_BIG_ENDIAN` and are not
meant to be set directly.

There is no `CONFIG_ZCBOR_VALIDATE`. `ZCBOR_MAP_SMART_SEARCH` is a raw
compile define in `zcbor_common.h`, not a Kconfig — to enable it, add
`zephyr_compile_definitions(ZCBOR_MAP_SMART_SEARCH)` in your
`CMakeLists.txt`.

### Minimal prj.conf

```kconfig
CONFIG_ZCBOR=y
CONFIG_ZCBOR_STOP_ON_ERROR=y
```

### Notes

- `CONFIG_ZCBOR_CANONICAL` is required for encoders that must conform
  to RFC 8949 canonical form (e.g. SUIT manifests).
- The `ZCBOR_MAP_SMART_SEARCH` compile define is only needed for
  `zcbor_unordered_map_search` when keys may appear multiple times.
- Default (non-canonical) mode encodes maps/lists with the
  indefinite-length terminator `0xFF`.

## Locations

### Source

| Resource           | Path                                                    |
|--------------------|---------------------------------------------------------|
| Encoding API       | `modules/zcbor/include/zcbor_encode.h`                  |
| Decoding API       | `modules/zcbor/include/zcbor_decode.h`                  |
| Shared types       | `modules/zcbor/include/zcbor_common.h`                  |
| Registered tags    | `modules/zcbor/include/zcbor_tags.h`                    |
| Implementation     | `modules/zcbor/src/{zcbor_encode,zcbor_decode,zcbor_common}.c` |

zcbor is a Zephyr module — no manual CMakeLists changes are needed
beyond `CONFIG_ZCBOR=y`, unless you add a CDDL codegen step (see the
overview above).

### Samples

```
samples/modules/zcbor/   # Basic encoding/decoding sample
```

### Tests

```
tests/subsys/mgmt/mcumgr/zcbor_bulk/  # zcbor used by MCUmgr
tests/net/lib/lwm2m/content_raw_cbor/
tests/net/lib/lwm2m/content_senml_cbor/
```

### Upstream

- GitHub: <https://github.com/NordicSemiconductor/zcbor>
- Upstream docs: <https://nordicsemiconductor.github.io/zcbor/latest/>
- Zephyr docs: <https://docs.zephyrproject.org/latest/services/serialization/cbor.html>

## Patterns

### List of Integers

```c
/* Encode */
zcbor_list_start_encode(zse, 3);
zcbor_int32_put(zse, 10);
zcbor_int32_put(zse, 20);
zcbor_int32_put(zse, 30);
zcbor_list_end_encode(zse, 3);

/* Decode */
int32_t vals[3];
zcbor_list_start_decode(zsd);
for (int i = 0; i < 3; i++) {
    zcbor_int32_decode(zsd, &vals[i]);
}
zcbor_list_end_decode(zsd);
```

### Unordered Map (Keys in Any Order)

```c
/* backups=1 required; n_flags = max elements in any unordered map */
ZCBOR_STATE_D(zsd, 1, buf, len, 1, 2);

int32_t temp;
struct zcbor_string unit;

zcbor_unordered_map_start_decode(zsd);
zcbor_search_key_tstr_lit(zsd, "temp") && zcbor_int32_decode(zsd, &temp);
zcbor_search_key_tstr_lit(zsd, "unit") && zcbor_tstr_decode(zsd, &unit);
zcbor_unordered_map_end_decode(zsd);
```

Add `zephyr_compile_definitions(ZCBOR_MAP_SMART_SEARCH)` if keys may appear
multiple times, or for optional fields. (It is a zcbor compile define, not a
Kconfig option.)

### Byte String (bstr)

```c
/* Encode raw bytes */
uint8_t data[] = {0x01, 0x02, 0x03};
zcbor_bstr_encode_ptr(zse, (char *)data, sizeof(data));

/* Decode — zero-copy: result.value points into the original buffer */
struct zcbor_string result;
zcbor_bstr_decode(zsd, &result);
/* result.value[0..result.len-1] valid while buf is alive */
```

### Nested CBOR in a bstr

```c
/* Encode: wrap inner CBOR inside a bstr */
zcbor_bstr_start_encode(zse);
zcbor_int32_put(zse, 42);
zcbor_bstr_end_encode(zse, NULL);

/* Decode */
struct zcbor_string inner_bstr;
zcbor_bstr_start_decode(zsd, &inner_bstr);
int32_t val;
zcbor_int32_decode(zsd, &val);
zcbor_bstr_end_decode(zsd);
```

### Error Handling

```c
bool ok = zcbor_map_start_encode(zse, 2)
       && zcbor_tstr_put_lit(zse, "key")
       && zcbor_int32_put(zse, 42)
       && zcbor_map_end_encode(zse, 2);

if (!ok) {
    int err = zcbor_peek_error(zse);  /* ZCBOR_ERR_* */
}
```

### Skipping Unknown Fields

```c
zcbor_any_skip(zsd, NULL);
```

### Dynamic Array with Multi-decode

```c
uint32_t items[8];
size_t num_decoded;

zcbor_list_start_decode(zsd);
zcbor_multi_decode(1, ARRAY_SIZE(items), &num_decoded,
                   (zcbor_decoder_t)zcbor_uint32_decode,
                   zsd, items, sizeof(items[0]));
zcbor_list_end_decode(zsd);
```
