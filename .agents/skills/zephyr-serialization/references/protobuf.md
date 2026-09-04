# Protocol Buffers (nanopb)

> **Prerequisite: nanopb is not cloned in this workspace.**
>
> `west.yml` uses a `name-allowlist` and does not include `nanopb`, so
> `deps/modules/lib/nanopb` does not exist and nothing here will build as
> written. No application in this repo currently uses protobuf.
>
> To use it, add the module to the allowlist in `west.yml` (keep the list
> alphabetical), then re-sync:
>
> ```yaml
>         name-allowlist:
>           ...
>           - mcuboot
>           - nanopb
>           - open-amp
> ```
>
> ```bash
> mise run west-update
> ```
>
> You also need the host `protoc` compiler available — nanopb's CMake
> integration invokes it at build time.
>
> Because the module is absent, the `CONFIG_NANOPB*` symbols below are
> **unverified against the tree** — `mise run check-skills` allowlists them
> for that reason rather than confirming them. Re-check them once the module is
> actually cloned. Everything in `./cbor.md` and `./json.md` *is* verified;
> prefer CBOR unless you specifically need protobuf schema compatibility with
> another system.

## Overview

Zephyr integrates [nanopb](https://jpa.kapsi.fi/nanopb/) — a small-footprint
C implementation of Google's [Protocol Buffers](https://protobuf.dev/) — as
a west module. You write `.proto` schemas, and a build-time generator
(`protoc` + the nanopb plugin) produces `.pb.h`/`.pb.c` files containing
C `struct`s and `pb_encode`/`pb_decode` entry points.

Unlike JSON or zcbor's manual API, nanopb is **schema-only**: every
message you encode or decode is described by a `.proto`. There's no
ad-hoc encoder.

### Quick Start

**1. Write a `.proto`**

```protobuf
// src/simple.proto
syntax = "proto3";

message SimpleMessage {
    int32 lucky_number = 1;
    bytes buffer = 2;
    int32 unlucky_number = 3;
}
```

**2. Add a sibling `.options` file** to bound variable-length fields
(required without `CONFIG_NANOPB_ENABLE_MALLOC`):

```
# src/simple.options
SimpleMessage.buffer max_size:8 fixed_length:true
```

**3. Wire it into CMake**:

```cmake
list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)

zephyr_nanopb_sources(app src/simple.proto)
```

`zephyr_nanopb_sources()` runs `protoc` at build time, adds the
generated `.pb.c` to the target, and exposes the `.pb.h` on the
include path. The sibling `.options` is picked up automatically.

**4. Use the generated API**:

```c
#include <pb_encode.h>
#include <pb_decode.h>
#include "src/simple.pb.h"   /* generated */

uint8_t buffer[SimpleMessage_size];
SimpleMessage msg = SimpleMessage_init_zero;

msg.lucky_number = 13;
for (int i = 0; i < sizeof(msg.buffer); ++i) {
    msg.buffer[i] = i * 2;
}

pb_ostream_t out = pb_ostream_from_buffer(buffer, sizeof(buffer));
if (!pb_encode(&out, SimpleMessage_fields, &msg)) {
    printk("Encode failed: %s\n", PB_GET_ERROR(&out));
}
size_t wire_len = out.bytes_written;

/* ...transmit `buffer[0..wire_len]`... */

SimpleMessage rx = SimpleMessage_init_zero;
pb_istream_t in = pb_istream_from_buffer(buffer, wire_len);
if (!pb_decode(&in, SimpleMessage_fields, &rx)) {
    printk("Decode failed: %s\n", PB_GET_ERROR(&in));
}

pb_release(SimpleMessage_fields, &rx);  /* only matters with malloc mode */
```

### Important Notes

1. **`protoc` is a build-time host dependency.** The build fails with
   `'protoc' not found` if it's missing. Install:
   `sudo apt install protobuf-compiler` /
   `brew install protobuf` /
   `choco install protoc`.
2. **Generated files live under `${CMAKE_CURRENT_BINARY_DIR}`** and are
   `#include`d with the path of the original `.proto` (e.g.
   `#include "src/simple.pb.h"` when the proto lived in `src/`). They
   are build artifacts — don't commit them, don't hand-edit them.
3. **All messages must be initialized.** Use the generated
   `<Msg>_init_zero` or `<Msg>_init_default`; un-initialized fields
   yield garbage `_count`/`has_*` values that confuse the encoder.
4. **String / bytes fields need `max_size`** (or
   `CONFIG_NANOPB_ENABLE_MALLOC`). proto3 doesn't carry size hints, so
   nanopb requires you to declare them in `.options`.
5. **`pb_encode` and `pb_decode` return `bool`.** Read
   `PB_GET_ERROR(stream)` for a human-readable message; suppress
   strings with `CONFIG_NANOPB_NO_ERRMSG=y` to save flash.
6. **Generated `_size` is an upper bound** suitable for sizing a stack
   buffer (`uint8_t buf[Msg_size]`). The actual encoded length is
   `stream.bytes_written`.

## References

- **API Details**: [#api](#api)
- **Kconfig Options**: [#kconfig](#kconfig)
- **Source Locations**: [#locations](#locations)
- **Patterns**: [#patterns](#patterns)

### Related Skills

- **zephyr-connectivity**: `references/sockets.md` (TCP/UDP transport)
- This skill, `references/json.md` — text-based alternative
- This skill, `references/cbor.md` — schemaless or CDDL-driven binary
  alternative

## Api

### Headers

```c
#include <pb.h>          /* core types: pb_byte_t, pb_callback_t, ... */
#include <pb_encode.h>   /* pb_encode, pb_ostream_t */
#include <pb_decode.h>   /* pb_decode, pb_istream_t */
#include <pb_common.h>   /* shared field iterator helpers (advanced use) */
#include "your_msg.pb.h" /* generated per-message types + entry points */
```

### Generated Symbols (per `MessageName`)

| Symbol                          | Meaning                                      |
|---------------------------------|----------------------------------------------|
| `MessageName`                   | Plain C struct with one field per proto field|
| `MessageName_init_zero`         | Init expression — all fields zeroed          |
| `MessageName_init_default`      | Init expression — proto defaults applied     |
| `MessageName_fields`            | Field descriptor array passed to encode/decode|
| `MessageName_size`              | Upper bound on encoded byte length           |
| `MessageName_msgid` (with option)| Numeric ID for routing                      |

### Field Mapping (proto3)

| Proto field                  | C field(s)                                   |
|------------------------------|----------------------------------------------|
| `int32 / int64 / uint32 / uint64` | matching `intN_t` / `uintN_t`             |
| `float`                      | `float`                                      |
| `double`                     | `double`                                     |
| `bool`                       | `bool`                                       |
| `string` (with `max_size:N`) | `char field[N]`                              |
| `bytes` (with `max_size:N`)  | `pb_byte_t field[N]` + `size_t field_size`   |
| `bytes` (`fixed_length:true`)| `pb_byte_t field[N]` (no size field)         |
| `repeated T` (with `max_count:N`)| `T field[N]` + `pb_size_t field_count`   |
| `optional T` (proto3)        | `bool has_field` + `T field`                 |
| nested `Message`             | `Message field` + `bool has_field`           |
| `oneof`                      | `union` + `int which_oneof` discriminator    |
| `enum E`                     | generated `enum _E { … }`, used by value     |

### Encoding

```c
typedef bool (*pb_callback_encode_t)(pb_ostream_t *stream,
                                     const pb_field_iter_t *field,
                                     void * const *arg);

bool pb_encode(pb_ostream_t *stream, const pb_msgdesc_t *fields,
               const void *src_struct);

bool pb_encode_delimited(pb_ostream_t *stream, const pb_msgdesc_t *fields,
                         const void *src_struct);

bool pb_encode_nullterminated(pb_ostream_t *stream,
                              const pb_msgdesc_t *fields,
                              const void *src_struct);

bool pb_encode_submessage(pb_ostream_t *stream, const pb_msgdesc_t *fields,
                          const void *src_struct);

bool pb_get_encoded_size(size_t *size, const pb_msgdesc_t *fields,
                         const void *src_struct);
```

- `pb_encode` writes the bare message.
- `pb_encode_delimited` prepends a varint length (useful for streaming
  multiple messages on a single connection).
- `pb_get_encoded_size` computes the exact byte length without
  encoding (useful for HTTP `Content-Length` etc.).

### Decoding

```c
bool pb_decode(pb_istream_t *stream, const pb_msgdesc_t *fields,
               void *dest_struct);

bool pb_decode_ex(pb_istream_t *stream, const pb_msgdesc_t *fields,
                  void *dest_struct, unsigned int flags);

bool pb_decode_delimited(pb_istream_t *stream, const pb_msgdesc_t *fields,
                         void *dest_struct);

bool pb_decode_nullterminated(pb_istream_t *stream,
                              const pb_msgdesc_t *fields,
                              void *dest_struct);

void pb_release(const pb_msgdesc_t *fields, void *dest_struct);
```

`pb_decode_ex` flags (combine with `|`):

- `PB_DECODE_NOINIT` — skip the implicit `init_zero` (caller pre-filled).
- `PB_DECODE_DELIMITED` — message is length-prefixed.
- `PB_DECODE_NULLTERMINATED` — message ends with a zero-tag terminator.

`pb_release` walks the descriptor and frees anything allocated under
`CONFIG_NANOPB_ENABLE_MALLOC`. It's a no-op for fixed-size fields, but
calling it unconditionally is the simplest pattern.

### Streams

```c
pb_ostream_t pb_ostream_from_buffer(pb_byte_t *buf, size_t bufsize);
pb_istream_t pb_istream_from_buffer(const pb_byte_t *buf, size_t bufsize);

/* Custom streams */
struct pb_ostream_s {
    bool (*callback)(pb_ostream_t *stream, const pb_byte_t *buf, size_t count);
    void *state;
    size_t max_size;
    size_t bytes_written;
    void *errmsg;
};

struct pb_istream_s {
    bool (*callback)(pb_istream_t *stream, pb_byte_t *buf, size_t count);
    void *state;
    size_t bytes_left;
    void *errmsg;
};
```

Custom streams are how you encode/decode directly to/from a socket or
file. Requires `CONFIG_NANOPB_BUFFER_ONLY=n` (the default).

### Errors

```c
#define PB_GET_ERROR(stream)   /* const char * — empty if NANOPB_NO_ERRMSG */
#define PB_RETURN_ERROR(stream, msg)
```

Always check the return of `pb_encode`/`pb_decode`; the error string
lives on the stream itself.

## Kconfig

### Required

```kconfig
CONFIG_NANOPB=y
```

Enables the nanopb library and runs the protoc-based generator.
Triggers a build-time `protoc` check; missing tools fail with a clear
error.

### Optional

```kconfig
# Allow runtime malloc for variable-length fields (no per-field max_size needed).
# Adds heap dependency — must also enable CONFIG_HEAP_MEM_POOL_SIZE or sys-heap.
CONFIG_NANOPB_ENABLE_MALLOC=y

# Max number of proto2 required fields checked for presence. Default & minimum 64.
CONFIG_NANOPB_MAX_REQUIRED_FIELDS=64

# Drop human-readable error strings (saves rodata).
CONFIG_NANOPB_NO_ERRMSG=y

# Disable custom streams; only memory-buffer encode/decode. Smaller + faster.
CONFIG_NANOPB_BUFFER_ONLY=y

# Strip int64 / uint64 support for 8-bit / old compilers.
CONFIG_NANOPB_WITHOUT_64BIT=y

# Emit scalar arrays unpacked (needed only for very old peer decoders).
CONFIG_NANOPB_ENCODE_ARRAYS_UNPACKED=y

# Validate UTF-8 on incoming `string` fields.
CONFIG_NANOPB_VALIDATE_UTF8=y
```

### Minimal prj.conf

```kconfig
CONFIG_NANOPB=y
```

### Footprint tuning

For the smallest binary on tight targets:

```kconfig
CONFIG_NANOPB=y
CONFIG_NANOPB_NO_ERRMSG=y
CONFIG_NANOPB_BUFFER_ONLY=y
CONFIG_NANOPB_WITHOUT_64BIT=y      # if your schema has no int64/uint64
```

### `.options` file syntax

A sibling `<name>.options` file alongside `<name>.proto` configures
per-field codegen. Common keys:

| Key             | Meaning                                                |
|-----------------|--------------------------------------------------------|
| `max_size:N`    | Array length for `string`, `bytes`, or repeated fields |
| `max_count:N`   | Max element count for `repeated` (alternate name)      |
| `max_length:N`  | Max `string` length excluding NUL                      |
| `fixed_length:true` | Strip the runtime `_size` field; always exactly `max_size` |
| `fixed_count:true`  | Strip the runtime `_count` field; always exactly `max_count` |
| `type:FT_*`     | Field kind: `FT_DEFAULT`, `FT_CALLBACK`, `FT_POINTER`, `FT_STATIC`, `FT_IGNORE` |
| `int_size:IS_*` | Force size: `IS_8`, `IS_16`, `IS_32`, `IS_64`          |

Example (covering several patterns):

```
SimpleMessage.buffer max_size:8 fixed_length:true
NestedMessage.name   max_size:32
SimpleMessage.unlucky_number type:FT_IGNORE
```

`type:FT_IGNORE` is useful for conditional fields — toggle it via
`<name>.options.in` + CMake `configure_file()` (see the
`samples/modules/nanopb` sample).

## Locations

### Module sources

| Resource                        | Path                                     |
|---------------------------------|------------------------------------------|
| Zephyr glue + CMake helper      | `modules/nanopb/nanopb.cmake`            |
| Zephyr Kconfig                  | `modules/nanopb/Kconfig`                 |
| Upstream nanopb library         | `modules/lib/nanopb/` (west module)      |
| `pb.h`, `pb_encode.h`, `pb_decode.h`, `pb_common.h` | shipped by the upstream module, on the include path when `CONFIG_NANOPB=y` |

### CMake API

```cmake
list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)

# Register .proto files against a target.
zephyr_nanopb_sources(<target> <proto-files>...)
```

Internally calls upstream `nanopb_generate_cpp` with
`RELPATH ${CMAKE_CURRENT_SOURCE_DIR}`, then adds the generated `.pb.c`
to `target_sources` and `${CMAKE_CURRENT_BINARY_DIR}` to the public
include dirs. Generated headers are wrapped in a `nanopb_generated_headers`
custom target so cross-target builds order correctly.

### Sample

```
samples/modules/nanopb/
├── CMakeLists.txt
├── prj.conf
├── Kconfig                  # sample-only options (buffer size, etc.)
└── src/
    ├── main.c
    ├── simple.proto
    └── simple.options.in    # configure_file()'d into simple.options
```

### Tests

```
tests/modules/nanopb/
├── proto/
│   ├── simple.proto / simple.options
│   ├── complex.proto              # `import "sub/nested.proto";`
│   └── sub/nested.proto / nested.options
└── src/main.c                     # Ztest cases
```

### Docs

- Zephyr docs: <https://docs.zephyrproject.org/latest/services/serialization/nanopb.html>
- Upstream nanopb docs: <https://jpa.kapsi.fi/nanopb/docs/>

## Patterns

### Nested Messages

```protobuf
// sub/nested.proto
syntax = "proto3";

message NestedMessage {
    uint32 id   = 1;
    string name = 2;
}
```

```protobuf
// complex.proto
syntax = "proto3";
import "sub/nested.proto";

message ComplexMessage {
    NestedMessage nested = 1;
}
```

```
# complex.options
sub/NestedMessage.name max_size:32
```

```c
ComplexMessage msg = ComplexMessage_init_zero;
msg.has_nested = true;
msg.nested.id  = 42;
strcpy(msg.nested.name, "Test name");

uint8_t buf[ComplexMessage_size];
pb_ostream_t out = pb_ostream_from_buffer(buf, sizeof(buf));
pb_encode(&out, ComplexMessage_fields, &msg);
```

The `has_<sub_msg>` flag is required — proto3 message-typed fields
behave as if marked `optional`.

### Repeated Fields

```protobuf
message SampleBatch {
    repeated float readings = 1;
}
```

```
SampleBatch.readings max_count:16
```

```c
SampleBatch batch = SampleBatch_init_zero;
batch.readings[0] = 21.5f;
batch.readings[1] = 22.0f;
batch.readings_count = 2;   /* must set explicitly */
```

For fixed-count arrays (e.g. always 4 channels), add
`fixed_count:true` — the `_count` field disappears.

### Variable-length bytes with malloc

```protobuf
message Payload {
    bytes data = 1;
}
```

```kconfig
CONFIG_NANOPB=y
CONFIG_NANOPB_ENABLE_MALLOC=y
CONFIG_HEAP_MEM_POOL_SIZE=4096
```

```c
Payload p = Payload_init_zero;

/* Decode allocates p.data on the heap */
pb_istream_t in = pb_istream_from_buffer(buf, len);
pb_decode(&in, Payload_fields, &p);

/* ...use p.data, p.data->size, p.data->bytes... */

pb_release(Payload_fields, &p);   /* mandatory in malloc mode */
```

### Length-delimited streaming

```c
/* Sender */
pb_encode_delimited(&out, MyMsg_fields, &msg1);
pb_encode_delimited(&out, MyMsg_fields, &msg2);

/* Receiver */
while (in.bytes_left > 0) {
    MyMsg msg = MyMsg_init_zero;
    if (!pb_decode_delimited(&in, MyMsg_fields, &msg)) {
        break;
    }
    /* ...handle msg... */
}
```

Pairs well with TCP transports where message boundaries aren't
preserved by the socket.

### Compile-time field toggling

`<name>.options.in` is a CMake-templated options file — useful for
gating fields on Kconfig:

```
# simple.options.in
SimpleMessage.buffer        max_size:@CONFIG_SAMPLE_BUFFER_SIZE@ fixed_length:true
SimpleMessage.unlucky_number type:@unlucky_number_type@
```

```cmake
if(CONFIG_SAMPLE_UNLUCKY_NUMBER)
  set(unlucky_number_type "FT_DEFAULT")
else()
  set(unlucky_number_type "FT_IGNORE")
endif()

# configure_file() of simple.options.in is automatic when the
# `.in` file sits next to the .proto and is referenced by name.
zephyr_nanopb_sources(app src/simple.proto)
```

### oneof

```protobuf
message Command {
    oneof payload {
        uint32 reboot_delay_ms = 1;
        string log_level       = 2;
    }
}
```

```c
Command cmd = Command_init_zero;
cmd.which_payload = Command_reboot_delay_ms_tag;
cmd.payload.reboot_delay_ms = 5000;
```

On decode, read `cmd.which_payload` first, then access the matching
union member.
