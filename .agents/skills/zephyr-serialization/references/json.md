# JSON Serialization

## Overview

Zephyr's JSON library provides compile-time descriptor-based serialization without dynamic memory allocation. It maps C struct fields to JSON keys using static descriptors.

### Quick Start

```c
#include <zephyr/data/json.h>

// 1. Define your data structure
struct sensor_data {
    const char *device_id;
    int32_t temperature;
    bool active;
};

// 2. Create descriptors mapping struct fields to JSON keys
static const struct json_obj_descr sensor_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct sensor_data, device_id, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct sensor_data, temperature, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct sensor_data, active, JSON_TOK_TRUE),
};

// 3. Encode to JSON
struct sensor_data data = {.device_id = "sensor-01", .temperature = 25, .active = true};
char buffer[128];
int ret = json_obj_encode_buf(sensor_descr, ARRAY_SIZE(sensor_descr), &data, buffer, sizeof(buffer));
// buffer: {"device_id":"sensor-01","temperature":25,"active":true}

// 4. Decode from JSON
char json[] = "{\"device_id\":\"sensor-02\",\"temperature\":30,\"active\":false}";
struct sensor_data parsed;
int64_t result = json_obj_parse(json, strlen(json), sensor_descr, ARRAY_SIZE(sensor_descr), &parsed);
```

### Kconfig

```kconfig
# Required
CONFIG_JSON_LIBRARY=y

# Optional: Enable float/double support (requires full libc)
CONFIG_JSON_LIBRARY_FP_SUPPORT=y
```

### Token Types Quick Reference

| Token Type | C Type | JSON Type |
|------------|--------|-----------|
| `JSON_TOK_STRING` | `const char *` | `"string"` |
| `JSON_TOK_STRING_BUF` | `char[]` | `"string"` (copies to buffer) |
| `JSON_TOK_NUMBER` | `int32_t` | `42` |
| `JSON_TOK_INT` | `int8_t`, `int16_t` | `-128` |
| `JSON_TOK_UINT` | `uint8_t`, `uint16_t`, `uint32_t` | `255` |
| `JSON_TOK_INT64` | `int64_t` | `9223372036854775807` |
| `JSON_TOK_UINT64` | `uint64_t` | `18446744073709551615` |
| `JSON_TOK_TRUE` / `JSON_TOK_FALSE` | `bool` | `true`/`false` |
| `JSON_TOK_FLOAT_FP` | `float` | `3.14` (requires FP_SUPPORT) |
| `JSON_TOK_DOUBLE_FP` | `double` | `3.14159` (requires FP_SUPPORT) |
| `JSON_TOK_OBJECT_START` | nested struct | `{...}` |
| `JSON_TOK_ARRAY_START` | array | `[...]` |

### Common Patterns

#### Nested Objects

```c
struct inner {
    int value;
};

struct outer {
    const char *name;
    struct inner nested;
};

static const struct json_obj_descr inner_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct inner, value, JSON_TOK_NUMBER),
};

static const struct json_obj_descr outer_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct outer, name, JSON_TOK_STRING),
    JSON_OBJ_DESCR_OBJECT(struct outer, nested, inner_descr),
};
// {"name":"test","nested":{"value":42}}
```

#### Arrays of Primitives

```c
struct data {
    int values[10];
    size_t values_len;  // Tracks actual count
};

static const struct json_obj_descr data_descr[] = {
    JSON_OBJ_DESCR_ARRAY(struct data, values, 10, values_len, JSON_TOK_NUMBER),
};
// {"values":[1,2,3,4,5]}
```

#### Arrays of Objects

```c
struct item {
    const char *name;
    int qty;
};

struct order {
    struct item items[5];
    size_t items_len;
};

static const struct json_obj_descr item_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct item, name, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct item, qty, JSON_TOK_NUMBER),
};

static const struct json_obj_descr order_descr[] = {
    JSON_OBJ_DESCR_OBJ_ARRAY(struct order, items, 5, items_len,
                             item_descr, ARRAY_SIZE(item_descr)),
};
// {"items":[{"name":"apple","qty":3},{"name":"banana","qty":2}]}
```

#### Named Fields (JSON key differs from C field)

Use `*_NAMED` variants when JSON keys aren't valid C identifiers:

```c
struct config {
    int api_version;     // JSON: "api-version"
    bool is_enabled;     // JSON: "is_enabled!"
};

static const struct json_obj_descr config_descr[] = {
    JSON_OBJ_DESCR_PRIM_NAMED(struct config, "api-version", api_version, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM_NAMED(struct config, "is_enabled!", is_enabled, JSON_TOK_TRUE),
};
```

#### String Buffers vs Pointers

```c
struct example {
    const char *ptr_string;    // Points into original JSON buffer
    char buf_string[32];       // Copies data (survives buffer reuse)
};

static const struct json_obj_descr example_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct example, ptr_string, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct example, buf_string, JSON_TOK_STRING_BUF),
};
```

**Use `JSON_TOK_STRING_BUF` when**: JSON buffer will be reused or freed after parsing.

### Important Notes

1. **Input modification**: `json_obj_parse()` modifies the input buffer (null-terminates strings in place)
2. **No UTF-8 validation**: The library does not validate UTF-8 encoding
3. **Escape handling**: Escape sequences are preserved as-is (e.g., `\t` stays as `\t`)
4. **Descriptor limit**: Maximum 63 fields per descriptor array
5. **Return value**: `json_obj_parse()` returns a bitmap of decoded fields (bit N = field N decoded)

### References

- **API Details**: [#api](#api) - Complete function signatures and return values
- **Kconfig Options**: [#kconfig](#kconfig) - Configuration options and dependencies
- **Source Locations**: [#locations](#locations) - Header, implementation, and sample paths

### Related Skills

- **zephyr-net-socket**: HTTP/CoAP communication (JSON payloads)
- **zephyr-settings**: Persistent storage (JSON config files)

## Api

### Header

```c
#include <zephyr/data/json.h>
```

### Core Functions

#### Parsing

```c
int64_t json_obj_parse(char *json, size_t len,
                       const struct json_obj_descr *descr, size_t descr_len,
                       void *val);
```

Parse JSON object into a C struct.

- **json**: Input buffer (MODIFIED in place - strings null-terminated)
- **len**: Length of JSON string
- **descr**: Descriptor array
- **descr_len**: Number of descriptors (max 63)
- **val**: Output struct pointer
- **Returns**: Bitmap of decoded fields (bit N set = field N decoded), negative on error

```c
int json_arr_parse(char *json, size_t len,
                   const struct json_obj_descr *descr, void *val);
```

Parse JSON array into a C struct containing an array field.

- **Returns**: 0 on success, negative errno on error

#### Encoding

```c
int json_obj_encode_buf(const struct json_obj_descr *descr, size_t descr_len,
                        const void *val, char *buffer, size_t buf_size);
```

Encode struct to JSON in a buffer.

- **buffer**: Output buffer
- **buf_size**: Buffer size (includes space for null terminator)
- **Returns**: 0 on success, negative errno on error

```c
int json_arr_encode_buf(const struct json_obj_descr *descr, const void *val,
                        char *buffer, size_t buf_size);
```

Encode array to JSON in a buffer.

```c
int json_obj_encode(const struct json_obj_descr *descr, size_t descr_len,
                    const void *val, json_append_bytes_t append_bytes,
                    void *data);
```

Encode using callback function (for streaming).

- **append_bytes**: `int (*)(const char *bytes, size_t len, void *data)`
- **Returns**: 0 on success, callback's error on failure

#### Length Calculation

```c
ssize_t json_calc_encoded_len(const struct json_obj_descr *descr,
                              size_t descr_len, const void *val);
```

Calculate buffer size needed for encoding (excludes null terminator).

```c
ssize_t json_calc_encoded_arr_len(const struct json_obj_descr *descr,
                                  const void *val);
```

Calculate buffer size for array encoding.

#### String Escaping

```c
ssize_t json_escape(char *str, size_t *len, size_t buf_size);
```

Escape string in-place for JSON encoding.

- **str**: String to escape (modified in place)
- **len**: Input/output length
- **buf_size**: Total buffer capacity
- **Returns**: 0 on success, -ENOMEM if insufficient space

```c
size_t json_calc_escaped_len(const char *str, size_t len);
```

Calculate escaped length without modifying string.

### Descriptor Macros

#### Primitives

```c
JSON_OBJ_DESCR_PRIM(struct_, field_name_, type_)
```

Describe a primitive field.

```c
JSON_OBJ_DESCR_PRIM_NAMED(struct_, "json-key", field_name_, type_)
```

Describe a primitive field with different JSON key name.

#### Objects

```c
JSON_OBJ_DESCR_OBJECT(struct_, field_name_, sub_descr_)
```

Describe a nested object field.

```c
JSON_OBJ_DESCR_OBJECT_NAMED(struct_, "json-key", field_name_, sub_descr_)
```

Describe nested object with different JSON key name.

#### Arrays

```c
JSON_OBJ_DESCR_ARRAY(struct_, field_name_, max_len_, len_field_, elem_type_)
```

Describe array of primitives.

- **max_len_**: Maximum array capacity
- **len_field_**: Field tracking actual element count

```c
JSON_OBJ_DESCR_ARRAY_NAMED(struct_, "json-key", field_name_, max_len_, len_field_, elem_type_)
```

Array with different JSON key name.

```c
JSON_OBJ_DESCR_OBJ_ARRAY(struct_, field_name_, max_len_, len_field_, elem_descr_, elem_descr_len_)
```

Describe array of objects.

```c
JSON_OBJ_DESCR_OBJ_ARRAY_NAMED(struct_, "json-key", field_name_, max_len_, len_field_, elem_descr_, elem_descr_len_)
```

Array of objects with different JSON key name.

```c
JSON_OBJ_DESCR_ARRAY_ARRAY(struct_, field_name_, max_len_, len_field_, elem_descr_, elem_descr_len_)
```

Describe 2D array (array of arrays).

#### Mixed Arrays

For arrays with heterogeneous element types:

```c
JSON_MIXED_ARR_DESCR_PRIM(struct_, field_name_, type_, count_field_)
JSON_MIXED_ARR_DESCR_OBJECT(struct_, field_name_, sub_descr_, count_field_)
JSON_MIXED_ARR_DESCR_ARRAY(struct_, field_name_, max_len_, elem_descr_, count_field_)
JSON_MIXED_ARR_DESCR_MIXED_ARRAY(struct_, field_name_, sub_descr_, count_field_)
```

```c
int json_mixed_arr_parse(char *json, size_t len,
                         const struct json_mixed_arr_descr *descr,
                         size_t descr_len, void *val);

int json_mixed_arr_encode_buf(const struct json_mixed_arr_descr *descr,
                              size_t descr_len, void *val,
                              char *buffer, size_t buf_size);
```

### Streaming Array Parsing

For parsing large arrays one object at a time:

```c
int json_arr_separate_object_parse_init(struct json_obj *json, char *payload, size_t len);
```

Initialize streaming array parser.

```c
int json_arr_separate_parse_object(struct json_obj *json,
                                   const struct json_obj_descr *descr,
                                   size_t descr_len, void *val);
```

Parse next object from array.

- **Returns**: Bitmap of decoded fields, 0 for end of array, negative on error

### Error Codes

| Return Value | Meaning |
|--------------|---------|
| `-EINVAL` | Invalid JSON syntax |
| `-ENOMEM` | Buffer too small |
| `-ENOENT` | Required field not found |
| `0` (encode) | Success |
| `bitmap` (parse) | Fields decoded (check bits) |

## Kconfig

### Required Configuration

```kconfig
CONFIG_JSON_LIBRARY=y
```

Enables the JSON parsing and encoding library.

### Optional Configuration

```kconfig
CONFIG_JSON_LIBRARY_FP_SUPPORT=y
```

Enables floating-point support (`float` and `double` types).

**Implications:**
- Automatically selects `CONFIG_CBPRINTF_FP_SUPPORT`
- Automatically selects `CONFIG_REQUIRES_FULL_LIBC`
- Requires libc with: `strtof()`, `strtod()`, `isnan()`, `isinf()`

**When to enable:**
- Parsing JSON with decimal numbers
- Sending sensor data with floating-point values
- Cloud protocols requiring float precision

### Example prj.conf

#### Minimal (integers only)

```ini
CONFIG_JSON_LIBRARY=y
```

#### With floating-point

```ini
CONFIG_JSON_LIBRARY=y
CONFIG_JSON_LIBRARY_FP_SUPPORT=y
```

#### For newlib (common on ARM)

```ini
CONFIG_JSON_LIBRARY=y
CONFIG_JSON_LIBRARY_FP_SUPPORT=y
CONFIG_NEWLIB_LIBC=y
```

#### For picolibc

```ini
CONFIG_JSON_LIBRARY=y
CONFIG_JSON_LIBRARY_FP_SUPPORT=y
CONFIG_PICOLIBC=y
```

### Memory Considerations

The JSON library is designed for minimal memory footprint:

- **No dynamic allocation**: All memory is stack/static
- **Descriptors**: Each descriptor is ~20-32 bytes (compile-time)
- **Buffer sizing**: Use `json_calc_encoded_len()` to determine exact buffer needs

#### Estimating Buffer Size

```c
// Calculate exact size needed
ssize_t needed = json_calc_encoded_len(descr, ARRAY_SIZE(descr), &data);
if (needed < 0) {
    // Handle error
}

char *buffer = k_malloc(needed + 1);  // +1 for null terminator
```

### Common Integration Patterns

#### HTTP Server

```ini
CONFIG_JSON_LIBRARY=y
CONFIG_NETWORKING=y
CONFIG_NET_SOCKETS=y
CONFIG_HTTP_SERVER=y
```

#### MQTT/Cloud

```ini
CONFIG_JSON_LIBRARY=y
CONFIG_JSON_LIBRARY_FP_SUPPORT=y
CONFIG_NETWORKING=y
CONFIG_MQTT_LIB=y
```

#### CoAP

```ini
CONFIG_JSON_LIBRARY=y
CONFIG_COAP=y
```

## Locations

### Zephyr Repository Paths

All paths relative to Zephyr root (`zephyr/`).

#### Core Library

| Resource | Path |
|----------|------|
| Header | `include/zephyr/data/json.h` |
| Implementation | `lib/utils/json.c` |
| Kconfig | `lib/utils/Kconfig` |

#### Documentation

| Resource | Path |
|----------|------|
| API docs | `doc/services/misc.rst` (JSON section) |

#### Tests

| Resource | Path |
|----------|------|
| Test source | `tests/lib/json/src/main.c` |
| Test config | `tests/lib/json/prj.conf` |
| Test spec | `tests/lib/json/testcase.yaml` |

The test file (`main.c`) contains comprehensive examples including:
- Nested objects and arrays
- Named fields with special characters
- Float/double encoding and decoding
- Mixed arrays
- 2D arrays
- Edge cases (limits, escaping)

### Sample Applications Using JSON

| Sample | Path | Description |
|--------|------|-------------|
| UpdateHub | `samples/subsys/mgmt/updatehub/` | OTA updates with JSON |
| AWS IoT | `samples/net/cloud/aws_iot_mqtt/` | Cloud telemetry |
| HTTP Server | `samples/net/sockets/http_server/` | REST API |
| HawkBit | `samples/subsys/mgmt/hawkbit/` | Device management |

### Running Tests

```bash
# Run JSON library tests
west twister -T tests/lib/json/

# Build for specific board
west build -b native_sim tests/lib/json
west build -t run
```

### Key Files to Reference

When implementing JSON:

1. **API usage patterns**: `tests/lib/json/src/main.c`
2. **Kconfig options**: `lib/utils/Kconfig`
3. **Error handling**: Check return values in `include/zephyr/data/json.h`

When debugging:

1. **Library source**: `lib/utils/json.c`
2. **Enable logging**: Add `CONFIG_LOG=y` and trace in application code
