---
name: zephyr-serialization
description: >
  Structured data serialization libraries shipped with Zephyr — JSON
  (descriptor-based, in-tree), CBOR (zcbor module, optionally driven
  by CDDL code generation), and Protocol Buffers (nanopb module,
  driven by .proto code generation). Use when encoding/decoding
  payloads for HTTP/MQTT/CoAP/LwM2M/SMP, persisting structured
  config, generating C types from a schema, or choosing between
  text and binary formats. Triggers on json_obj_descr,
  json_obj_parse, JSON_TOK_*, zcbor_*, ZCBOR_STATE_*, CDDL,
  pb_encode, pb_decode, .proto / .cddl files, CONFIG_JSON_LIBRARY,
  CONFIG_ZCBOR, CONFIG_NANOPB.
---

# Zephyr Serialization

Validated against: Zephyr 4.4.99 (a632b9723bab, 2026-08-07). Re-check with `mise run check-skills`.

## Scope

Zephyr's `doc/services/serialization/` groups three first-class
serialization libraries:

- **JSON** — in-tree, descriptor-based, no codegen, text wire format.
- **CBOR (zcbor)** — west module, manual API *or* CDDL → C codegen,
  binary wire format used by CoAP/LwM2M/SUIT/SMP.
- **Protocol Buffers (nanopb)** — west module, `.proto` → C codegen,
  binary wire format used by gRPC-adjacent and embedded RPC stacks.

This skill covers all three. It does NOT cover the *transports* that
carry these payloads (CoAP/HTTP/MQTT/SMP) — see `zephyr-connectivity`.

## Pick the right reference

| You're working on...                                                 | Load                         |
|----------------------------------------------------------------------|------------------------------|
| `json_obj_descr[]`, `json_obj_parse`, `JSON_TOK_*`, FP support       | `references/json.md`         |
| `zcbor_*` manual API, `ZCBOR_STATE_*`, CDDL schemas, zcbor code-gen  | `references/cbor.md`         |
| `.proto` files, `pb_encode`/`pb_decode`, `zephyr_nanopb_sources()`   | `references/protobuf.md` ⚠️   |

⚠️ **nanopb is not in this workspace's `west.yml` allowlist**, so
`deps/modules/lib/nanopb` does not exist and no app here uses protobuf. Adding
the module is a one-line `west.yml` change plus `mise run west-update` — see the note
at the top of `references/protobuf.md`. Its `CONFIG_NANOPB*` symbols are
consequently unverified.

## Choosing a format

| Need...                                                | Pick                          |
|--------------------------------------------------------|-------------------------------|
| Human-readable wire format, REST/web interop           | JSON                          |
| Smallest binary, schema in version-controlled `.cddl`  | CBOR + CDDL codegen           |
| Existing IoT protocol payload (CoAP/LwM2M/SUIT/SMP)    | CBOR — schemas already exist  |
| Cross-language RPC with proto3 ecosystem               | Protobuf (nanopb) — needs the module added first |
| No codegen toolchain available at build time           | JSON or manual zcbor          |
| Fixed-size, predictable RAM footprint                  | Protobuf (sized via options)  |

## Universal traps

- **JSON descriptors are runtime-checked, not compile-time.** Field
  mismatch between `struct` and `json_obj_descr[]` is a parse error,
  not a compile error. `json_obj_parse()` *modifies* the input buffer.
- **zcbor returns `bool`, not an errno.** Chain calls with `&&` and
  inspect `zcbor_peek_error()` on failure. Without
  `CONFIG_ZCBOR_STOP_ON_ERROR`, later calls keep executing after the
  first failure and may corrupt the cursor.
- **CDDL and `.proto` codegen outputs are build artifacts.** Never
  hand-edit them; regenerate from the schema. Wire regeneration into
  CMake (`add_custom_command DEPENDS ${schema}` for zcbor;
  `zephyr_nanopb_sources()` for nanopb) so stale generated code can't
  ship.
- **Float/double support is opt-in.** JSON needs
  `CONFIG_JSON_LIBRARY_FP_SUPPORT=y` (pulls full libc). zcbor has it
  by default but a CDDL schema must explicitly use `float`. nanopb
  requires `float`/`double` in the `.proto`; no Kconfig gate.
- **All three libraries are non-allocating by default.** Nanopb has
  `CONFIG_NANOPB_ENABLE_MALLOC` for variable-length fields; without
  it, every repeated/string field needs a `max_size` in `.options`.

## Related skills

- `zephyr-connectivity` — transports that carry serialized payloads
  (sockets/HTTP/CoAP, BLE/SMP).
- `zephyr-system` — `references/settings.md` and `references/storage.md`
  for persisting serialized blobs on flash.
