# Devicetree

## Overview

Expert guidance on Zephyr's devicetree system for hardware description, driver integration, and build-time code generation.

### Table of Contents

1. [Core Concepts](#core-concepts)
2. [Common Workflows](#common-workflows)
3. [Hardware Configuration](#hardware-configuration)
4. [Advanced Topics](#advanced-topics)
5. [Troubleshooting](#troubleshooting)

---

### Core Concepts

#### Understanding Devicetree Structure
Devicetree is a tree of nodes and properties. Zephyr uses **bindings** to validate nodes and generate C macros.

- **Basic syntax, nodes, properties**: See [syntax.md](#syntax)
- **Bindings and compatible matching**: See [bindings.md](#bindings)
- **Address translation (#address-cells, #size-cells, ranges)**: See [address-translation.md](#address-translation)

#### Key Files in a Build
After building, check these for debugging:
- `build/zephyr/zephyr.dts` — Final merged devicetree
- `build/zephyr/include/generated/devicetree_generated.h` — Generated macros

---

### Common Workflows

#### 1. Modifying Hardware via Overlays
Overlays customize hardware without modifying base Zephyr files.

- **Override properties, add nodes, enable/disable devices**: See [overlays.md](#overlays)

#### 2. Writing Custom Bindings
Bindings define the schema for devicetree nodes.

- **Basic binding structure**: See [bindings.md](#bindings)
- **Advanced features (child-binding, enums, specifier-cells)**: See [advanced-bindings.md](#advanced-bindings)

#### 3. Accessing Devicetree from C Code
Zephyr provides compile-time macros to access devicetree data.

- **Basic macros (node IDs, properties, registers)**: See [macros.md](#macros)
- **Advanced macros (iteration, strings, bus helpers)**: See [advanced-macros.md](#advanced-macros)

---

### Hardware Configuration

#### Pin Control (Pinctrl)
Configure pin multiplexing and electrical properties.
- See [pinctrl.md](#pinctrl)

#### Clocks
Configure clock providers and consumers.
- See [clocks.md](#clocks)

#### Interrupts
Configure interrupt controllers and consumers with multi-level support.
- See [interrupts.md](#interrupts)

#### DMA
Configure DMA controllers and channel assignments.
- See [dma.md](#dma)

---

### Advanced Topics

#### Address Translation
Understanding `#address-cells`, `#size-cells`, and `ranges` for complex SoC hierarchies.
- See [address-translation.md](#address-translation)

#### Advanced Binding Features
Child bindings, enums, const, specifier-cells, and binding inheritance.
- See [advanced-bindings.md](#advanced-bindings)

#### Advanced C Macros
Iteration macros, string helpers, and bus-specific conveniences.
- See [advanced-macros.md](#advanced-macros)

---

### Troubleshooting

For common errors and debugging techniques:
- See [debugging.md](#debugging)

#### Quick Reference

| Error | Likely Cause | Fix |
|-------|--------------|-----|
| "binding for ... not found" | Missing or mismatched `compatible` | Check binding exists and `compatible` matches |
| `DEVICE_DT_GET()` returns NULL | Node disabled | Add `status = "okay";` |
| `__device_dts_ord_N` linker error | Driver not enabled | Enable driver in Kconfig |
| Property type mismatch | Binding expects different type | Check binding's `type:` field |

---

### Practical Examples

See [examples.md](#examples) for complete working examples:
- GPIO LED (Blinky)
- I2C Sensor
- SPI Flash with Partitions
- PWM Buzzer
- UART with Pin Control
- ADC Channel Configuration
- CAN Bus Setup
- Timer/Counter Configuration

## Address Translation

Address translation describes how addresses are mapped between different levels of the hardware hierarchy using `#address-cells`, `#size-cells`, and `ranges`.

### Overview

Devicetree addresses are **hierarchical**. Child node addresses are in the parent's address space. Translation properties define how to convert between address spaces.

### Core Properties

| Property | Description |
|----------|-------------|
| `#address-cells` | Number of 32-bit cells for addresses in child nodes |
| `#size-cells` | Number of 32-bit cells for sizes in child nodes |
| `ranges` | Address translation from child to parent space |
| `reg` | Address and size of the node's resources |

### Basic Example

```devicetree
/ {
    #address-cells = <1>;  /* 32-bit addresses */
    #size-cells = <1>;     /* 32-bit sizes */

    soc@40000000 {
        compatible = "simple-bus";
        #address-cells = <1>;
        #size-cells = <1>;
        ranges = <0x0 0x40000000 0x10000000>;
        /* Child 0x0 = Parent 0x40000000, size 256MB */

        uart0: uart@1000 {
            reg = <0x1000 0x100>;
            /* Actual address: 0x40000000 + 0x1000 = 0x40001000 */
        };
    };
};
```

### #address-cells and #size-cells

These define the format of `reg` properties in child nodes:

```devicetree
/* 32-bit addresses, 32-bit sizes */
#address-cells = <1>;
#size-cells = <1>;
child { reg = <0x1000 0x100>; };  /* addr=0x1000, size=0x100 */

/* 64-bit addresses, 64-bit sizes */
#address-cells = <2>;
#size-cells = <2>;
child { reg = <0x0 0x80000000 0x0 0x1000>; };  /* addr=0x80000000, size=0x1000 */

/* 32-bit addresses, no size (e.g., I2C) */
#address-cells = <1>;
#size-cells = <0>;
child { reg = <0x48>; };  /* I2C address 0x48 */
```

### The ranges Property

`ranges` translates addresses from child to parent address space.

#### Format
```
ranges = <child_addr parent_addr size> [, ...];
```

#### Empty ranges (Identity Mapping)
```devicetree
soc {
    ranges;  /* Empty = identity mapping (1:1) */
    /* Child addresses = parent addresses */
};
```

#### Simple Translation
```devicetree
soc@40000000 {
    #address-cells = <1>;
    #size-cells = <1>;
    ranges = <0x0 0x40000000 0x10000000>;
    /*
     * Child address 0x0 maps to parent 0x40000000
     * Translation window is 256MB (0x10000000)
     */
};
```

#### Multiple Ranges
```devicetree
pcie@10000000 {
    #address-cells = <3>;  /* PCI uses 3 cells */
    #size-cells = <2>;
    ranges = <0x02000000 0x0 0x10000000   /* 32-bit memory */
              0x0 0x10000000
              0x0 0x01000000>,
             <0x01000000 0x0 0x00000000   /* I/O space */
              0x0 0x03000000
              0x0 0x00010000>;
};
```

### Common Patterns

#### SoC with Peripherals
```devicetree
/ {
    #address-cells = <1>;
    #size-cells = <1>;

    soc {
        compatible = "simple-bus";
        #address-cells = <1>;
        #size-cells = <1>;
        ranges;  /* Identity - SoC addresses = CPU addresses */

        peripheral@40000000 {
            reg = <0x40000000 0x1000>;
        };
    };
};
```

#### Memory Regions
```devicetree
/ {
    #address-cells = <1>;
    #size-cells = <1>;

    sram0: memory@20000000 {
        compatible = "mmio-sram";
        reg = <0x20000000 0x40000>;  /* 256KB SRAM */
    };

    flash0: flash@8000000 {
        compatible = "soc-nv-flash";
        reg = <0x08000000 0x100000>;  /* 1MB Flash */
    };
};
```

#### External Memory Bus
```devicetree
external-bus@50000000 {
    compatible = "simple-bus";
    #address-cells = <2>;  /* chip-select + offset */
    #size-cells = <1>;
    ranges = <0 0 0x50000000 0x1000000>,  /* CS0 -> 0x50000000 */
             <1 0 0x51000000 0x1000000>,  /* CS1 -> 0x51000000 */
             <2 0 0x52000000 0x1000000>;  /* CS2 -> 0x52000000 */

    flash@0,0 {
        reg = <0 0x0 0x100000>;  /* CS0, offset 0, 1MB */
    };

    ethernet@1,0 {
        reg = <1 0x0 0x1000>;  /* CS1, offset 0, 4KB */
    };
};
```

#### I2C Bus (No Size)
```devicetree
i2c0: i2c@40003000 {
    compatible = "vendor,i2c";
    reg = <0x40003000 0x1000>;
    #address-cells = <1>;
    #size-cells = <0>;  /* I2C has no size concept */

    sensor@48 {
        compatible = "ti,tmp102";
        reg = <0x48>;  /* Just the I2C address */
    };

    eeprom@50 {
        compatible = "atmel,24c32";
        reg = <0x50>;
    };
};
```

#### SPI Bus (Chip Select)
```devicetree
spi0: spi@40004000 {
    compatible = "vendor,spi";
    reg = <0x40004000 0x1000>;
    #address-cells = <1>;
    #size-cells = <0>;

    flash@0 {
        compatible = "jedec,spi-nor";
        reg = <0>;  /* Chip select 0 */
        spi-max-frequency = <40000000>;
    };

    display@1 {
        compatible = "sitronix,st7789v";
        reg = <1>;  /* Chip select 1 */
    };
};
```

### Zephyr-Specific Patterns

#### Chosen Memory Regions
```devicetree
/ {
    chosen {
        zephyr,sram = &sram0;
        zephyr,flash = &flash0;
        zephyr,code-partition = &slot0_partition;
    };
};
```

#### Flash Partitions
```devicetree
&flash0 {
    partitions {
        compatible = "fixed-partitions";
        #address-cells = <1>;
        #size-cells = <1>;

        boot_partition: partition@0 {
            label = "mcuboot";
            reg = <0x0 0x10000>;
        };
        slot0_partition: partition@10000 {
            label = "image-0";
            reg = <0x10000 0x70000>;
        };
        storage_partition: partition@80000 {
            label = "storage";
            reg = <0x80000 0x80000>;
        };
    };
};
```

### C API

```c
/* Get register address and size */
#define MY_NODE DT_NODELABEL(uart0)

#define MY_REG_ADDR DT_REG_ADDR(MY_NODE)     /* Base address */
#define MY_REG_SIZE DT_REG_SIZE(MY_NODE)     /* Size */

/* Multiple register blocks */
DT_REG_ADDR_BY_IDX(node_id, idx)  /* Nth register address */
DT_REG_SIZE_BY_IDX(node_id, idx)  /* Nth register size */
DT_NUM_REGS(node_id)              /* Number of reg entries */

/* Named registers (if reg-names exists) */
DT_REG_ADDR_BY_NAME(node_id, name)
DT_REG_SIZE_BY_NAME(node_id, name)
```

### Tips

1. **Check parent's cells** — `reg` format depends on parent's `#address-cells` and `#size-cells`
2. **Empty ranges** — Use `ranges;` for identity mapping (most common in Zephyr)
3. **Bus nodes** — Buses like I2C/SPI use `#size-cells = <0>`
4. **Final addresses** — Check `build/zephyr/zephyr.dts` to see resolved addresses
5. **64-bit systems** — Use `#address-cells = <2>` for addresses > 4GB

## Advanced Bindings

Advanced binding features for complex hardware descriptions including child bindings, enums, specifier cells, and inheritance.

### Child Bindings

Child bindings constrain the format of child nodes. Used for containers like `gpio-leds`, `pwm-leds`, etc.

#### Basic Child Binding

```yaml
# gpio-leds.yaml
compatible: "gpio-leds"
description: Container for GPIO-connected LEDs

child-binding:
  description: An LED connected to a GPIO
  properties:
    gpios:
      type: phandle-array
      required: true
    label:
      type: string
```

**DTS Usage:**
```devicetree
leds {
    compatible = "gpio-leds";
    led0: led_0 {
        gpios = <&gpio0 13 GPIO_ACTIVE_LOW>;
        label = "Green LED";
    };
    led1: led_1 {
        gpios = <&gpio0 14 GPIO_ACTIVE_LOW>;
        label = "Red LED";
    };
};
```

#### Nested Child Bindings

For deeper hierarchies (grandchildren):

```yaml
# fixed-partitions.yaml
compatible: "fixed-partitions"

properties:
  "#address-cells":
    const: 1
  "#size-cells":
    const: 1

child-binding:
  description: A flash partition
  properties:
    reg:
      type: array
      required: true
    label:
      type: string
    read-only:
      type: boolean
```

### Enum and Const Properties

#### Enum (Restrict to List)

```yaml
properties:
  operating-mode:
    type: string
    enum:
      - "low-power"
      - "normal"
      - "high-performance"
    default: "normal"
    description: Device operating mode

  trigger-type:
    type: int
    enum: [0, 1, 2, 4]
    description: Interrupt trigger type
```

**DTS Usage:**
```devicetree
my_device {
    operating-mode = "high-performance";
    trigger-type = <2>;
};
```

**C Access:**
```c
/* Get enum as index (0, 1, 2...) */
DT_ENUM_IDX(node_id, operating_mode)

/* Get enum as token for switch statements */
DT_STRING_TOKEN(node_id, operating_mode)
```

#### Const (Fixed Value)

```yaml
properties:
  "#address-cells":
    type: int
    const: 1
  "#size-cells":
    type: int
    const: 0
  compatible:
    const: "vendor,specific-device"
```

The build fails if DTS doesn't match the const value.

### Specifier Cells

Specifier cells name the elements in phandle-arrays, enabling named access in C.

#### GPIO Cells

```yaml
# gpio-controller.yaml
gpio-cells:
  - pin
  - flags
```

**DTS:**
```devicetree
led {
    gpios = <&gpio0 13 GPIO_ACTIVE_LOW>;
    /*       ^phandle ^pin ^flags */
};
```

**C Access:**
```c
DT_GPIO_PIN(node_id, gpios)    /* Returns 13 */
DT_GPIO_FLAGS(node_id, gpios)  /* Returns GPIO_ACTIVE_LOW */
/* Or generically: */
DT_PHA(node_id, gpios, pin)
DT_PHA(node_id, gpios, flags)
```

#### PWM Cells

```yaml
# pwm-controller.yaml
pwm-cells:
  - channel
  - period
  - flags
```

**DTS:**
```devicetree
buzzer {
    pwms = <&pwm0 2 1000000 PWM_POLARITY_NORMAL>;
    /*     ^phandle ^ch ^period ^flags */
};
```

**C Access:**
```c
DT_PWMS_CHANNEL(node_id, buzzer)
DT_PWMS_PERIOD(node_id, buzzer)
DT_PWMS_FLAGS(node_id, buzzer)
/* Or: */
DT_PHA(node_id, pwms, channel)
DT_PHA(node_id, pwms, period)
```

#### Custom Cells

```yaml
# my-controller.yaml
my-cells:
  - index
  - config
  - mode
```

**DTS:**
```devicetree
consumer {
    my-controller = <&my_ctrl 0 0x1234 2>;
};
```

**C Access:**
```c
DT_PHA(node_id, my_controller, index)   /* 0 */
DT_PHA(node_id, my_controller, config)  /* 0x1234 */
DT_PHA(node_id, my_controller, mode)    /* 2 */
```

### Binding Inheritance (include)

#### Basic Include

```yaml
# vendor,uart.yaml
compatible: "vendor,uart"
include: base.yaml

properties:
  current-speed:
    type: int
    default: 115200
```

#### Multiple Includes

```yaml
include:
  - base.yaml
  - uart-controller.yaml
  - pinctrl-device.yaml
```

#### Include with Overrides

```yaml
include:
  - name: base.yaml
    property-allowlist:
      - reg
      - interrupts
      - status

  - name: uart-controller.yaml
    property-blocklist:
      - deprecated-property
```

#### Common Base Bindings

| Binding | Provides |
|---------|----------|
| `base.yaml` | `reg`, `status`, `compatible`, `label` |
| `i2c-device.yaml` | I2C slave properties |
| `spi-device.yaml` | SPI slave properties |
| `gpio-controller.yaml` | GPIO controller properties |
| `interrupt-controller.yaml` | Interrupt controller properties |
| `pinctrl-device.yaml` | Pin control properties |

### Deprecated Properties

```yaml
properties:
  old-property:
    type: int
    deprecated: true
    description: Use 'new-property' instead

  new-property:
    type: int
```

Build warns if deprecated properties are used.

### Property Dependencies

```yaml
properties:
  feature-enabled:
    type: boolean

  feature-config:
    type: int
    description: Only valid if feature-enabled is set
```

Note: Zephyr bindings don't enforce property dependencies at build time, but document them.

### Complete Binding Example

```yaml
# vendor,my-sensor.yaml
description: A custom environmental sensor

compatible: "vendor,my-sensor"

include:
  - base.yaml
  - i2c-device.yaml

properties:
  reg:
    required: true

  sample-rate:
    type: int
    enum: [1, 10, 100, 1000]
    default: 100
    description: Sampling rate in Hz

  temperature-offset:
    type: int
    default: 0
    description: Temperature calibration offset (m°C)

  power-mode:
    type: string
    enum:
      - "ultra-low"
      - "low"
      - "normal"
    default: "normal"

  interrupt-gpios:
    type: phandle-array
    description: Optional data-ready interrupt
```

**Usage:**
```devicetree
&i2c1 {
    my_sensor: sensor@44 {
        compatible = "vendor,my-sensor";
        reg = <0x44>;
        sample-rate = <100>;
        power-mode = "low";
        interrupt-gpios = <&gpio0 5 GPIO_ACTIVE_LOW>;
    };
};
```

### Tips

1. **Start with base.yaml** — Always include it for standard properties
2. **Use enums** — Constrain values to prevent typos
3. **Name your cells** — Makes C code more readable
4. **Document well** — Binding descriptions appear in build errors
5. **Check existing bindings** — Look at `dts/bindings/` for patterns

## Advanced Macros

Iteration macros, string helpers, and bus-specific conveniences beyond basic property access.

### Iteration Macros

#### DT_FOREACH_STATUS_OKAY

Iterate over all nodes with a given compatible that are enabled:

```c
#define DT_DRV_COMPAT vendor_device

/* Generate code for each instance */
#define CREATE_INSTANCE(inst) \
    static struct my_data data_##inst; \
    static const struct my_config config_##inst = { \
        .reg = DT_INST_REG_ADDR(inst), \
    }; \
    DEVICE_DT_INST_DEFINE(inst, my_init, NULL, \
                          &data_##inst, &config_##inst, \
                          POST_KERNEL, CONFIG_MY_INIT_PRIORITY, \
                          &my_api);

DT_INST_FOREACH_STATUS_OKAY(CREATE_INSTANCE)
```

#### DT_FOREACH_CHILD

Iterate over all children of a node:

```c
#define LED_NODE DT_PATH(leds)

#define COUNT_LED(node_id) + 1
#define NUM_LEDS (0 DT_FOREACH_CHILD(LED_NODE, COUNT_LED))

#define LED_GPIO_SPEC(node_id) GPIO_DT_SPEC_GET(node_id, gpios),
static const struct gpio_dt_spec leds[] = {
    DT_FOREACH_CHILD(LED_NODE, LED_GPIO_SPEC)
};
```

#### DT_FOREACH_CHILD_STATUS_OKAY

Same as above, but only enabled children:

```c
#define INIT_BUTTON(node_id) init_button(GPIO_DT_SPEC_GET(node_id, gpios));

void init_all_buttons(void) {
    DT_FOREACH_CHILD_STATUS_OKAY(DT_PATH(buttons), INIT_BUTTON)
}
```

#### DT_FOREACH_PROP_ELEM

Iterate over array property elements:

```c
#define MY_NODE DT_NODELABEL(my_device)

/* For array property: my-values = <1 2 3 4>; */
#define PRINT_ELEM(node_id, prop, idx) \
    printk("Element %d: %d\n", idx, DT_PROP_BY_IDX(node_id, prop, idx));

void print_values(void) {
    DT_FOREACH_PROP_ELEM(MY_NODE, my_values, PRINT_ELEM)
}

/* Collect into array */
#define GET_ELEM(node_id, prop, idx) DT_PROP_BY_IDX(node_id, prop, idx),
static const int values[] = {
    DT_FOREACH_PROP_ELEM(MY_NODE, my_values, GET_ELEM)
};
```

#### DT_FOREACH_PROP_ELEM_SEP

With custom separator:

```c
#define GET_VAL(node_id, prop, idx) DT_PROP_BY_IDX(node_id, prop, idx)

/* Generates: val1 | val2 | val3 */
#define MY_FLAGS DT_FOREACH_PROP_ELEM_SEP(MY_NODE, flags, GET_VAL, (|))
```

### String Macros

#### DT_STRING_TOKEN

Convert string property to C token (for enums, switch statements):

```c
/* DTS: operating-mode = "high-performance"; */

#define MODE DT_STRING_TOKEN(MY_NODE, operating_mode)
/* Expands to: high_performance (underscores replace hyphens) */

switch (MODE) {
    case low_power: /* ... */ break;
    case normal: /* ... */ break;
    case high_performance: /* ... */ break;
}
```

#### DT_STRING_UPPER_TOKEN

Uppercase version:

```c
#define MODE DT_STRING_UPPER_TOKEN(MY_NODE, operating_mode)
/* Expands to: HIGH_PERFORMANCE */
```

#### DT_STRING_UNQUOTED

Get string without quotes (for macro concatenation):

```c
/* DTS: prefix = "my"; */
#define PREFIX DT_STRING_UNQUOTED(MY_NODE, prefix)
/* Can use in: PREFIX##_function() */
```

#### DT_PROP_BY_IDX for String Arrays

```c
/* DTS: names = "alice", "bob", "charlie"; */

const char *name0 = DT_PROP_BY_IDX(MY_NODE, names, 0);  /* "alice" */
const char *name1 = DT_PROP_BY_IDX(MY_NODE, names, 1);  /* "bob" */
```

### Bus-Specific Macros

#### GPIO

```c
#define MY_NODE DT_NODELABEL(my_device)

/* Get GPIO spec (preferred) */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(MY_NODE, gpios);
/* Or with index: */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET_BY_IDX(MY_NODE, gpios, 0);

/* Individual components */
DT_GPIO_CTLR(node_id, prop)         /* GPIO controller phandle */
DT_GPIO_PIN(node_id, prop)          /* Pin number */
DT_GPIO_FLAGS(node_id, prop)        /* Flags */

/* Usage */
gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
gpio_pin_set_dt(&led, 1);
```

#### I2C

```c
#define SENSOR_NODE DT_NODELABEL(my_sensor)

/* Get I2C device spec */
static const struct i2c_dt_spec sensor = I2C_DT_SPEC_GET(SENSOR_NODE);

/* Components */
DT_REG_ADDR(SENSOR_NODE)  /* I2C address */
DT_BUS(SENSOR_NODE)       /* Parent I2C controller node */

/* Usage */
i2c_write_dt(&sensor, data, sizeof(data));
```

#### SPI

```c
#define FLASH_NODE DT_NODELABEL(ext_flash)

/* Get SPI device spec */
static const struct spi_dt_spec flash = SPI_DT_SPEC_GET(FLASH_NODE,
                                                        SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
                                                        0);

/* Components */
DT_REG_ADDR(FLASH_NODE)                /* Chip select index */
DT_PROP(FLASH_NODE, spi_max_frequency) /* Max frequency */

/* Usage */
spi_write_dt(&flash, &tx_bufs);
```

#### PWM

```c
#define BUZZER_NODE DT_NODELABEL(buzzer)

/* Get PWM spec */
static const struct pwm_dt_spec buzzer = PWM_DT_SPEC_GET(BUZZER_NODE);

/* Components */
DT_PWMS_CTLR(node_id)     /* PWM controller */
DT_PWMS_CHANNEL(node_id)  /* Channel */
DT_PWMS_PERIOD(node_id)   /* Period in ns */
DT_PWMS_FLAGS(node_id)    /* Flags */

/* Usage */
pwm_set_pulse_dt(&buzzer, period / 2);
```

### Existence and Conditional Macros

#### Node Existence

```c
#if DT_NODE_EXISTS(DT_NODELABEL(optional_device))
    /* Code for when node exists */
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart0), okay)
    /* Code for when node is enabled */
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(vendor_device)
    /* At least one vendor,device node is enabled */
#endif
```

#### Property Existence

```c
#if DT_NODE_HAS_PROP(MY_NODE, optional_prop)
    int val = DT_PROP(MY_NODE, optional_prop);
#else
    int val = DEFAULT_VALUE;
#endif

/* Or use default: */
int val = DT_PROP_OR(MY_NODE, optional_prop, DEFAULT_VALUE);
```

#### Compile-Time Conditionals

```c
/* COND versions return 1 or 0 instead of being undefined */
BUILD_ASSERT(DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_console), okay),
             "Console device must be enabled");

/* Use in ternary */
#define MY_SIZE DT_COND_NODE_HAS_PROP(MY_NODE, size, \
                                       DT_PROP(MY_NODE, size), \
                                       DEFAULT_SIZE)
```

### Instance Macros (For Drivers)

When writing drivers, use instance macros with `DT_DRV_COMPAT`:

```c
#define DT_DRV_COMPAT vendor_my_device

/* Instance versions of all macros */
DT_INST_REG_ADDR(inst)
DT_INST_PROP(inst, property)
DT_INST_IRQN(inst)
DT_INST_GPIO_PIN(inst, gpios)
DT_INST_FOREACH_STATUS_OKAY(fn)

/* Example driver pattern */
#define MY_DEVICE_INIT(inst)                                    \
    static struct my_data my_data_##inst;                       \
    static const struct my_config my_config_##inst = {          \
        .base = DT_INST_REG_ADDR(inst),                         \
        .irq = DT_INST_IRQN(inst),                              \
        .speed = DT_INST_PROP_OR(inst, speed, 100000),          \
    };                                                          \
    DEVICE_DT_INST_DEFINE(inst,                                 \
                          my_init,                              \
                          NULL,                                 \
                          &my_data_##inst,                      \
                          &my_config_##inst,                    \
                          POST_KERNEL,                          \
                          CONFIG_MY_DRIVER_INIT_PRIORITY,       \
                          &my_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MY_DEVICE_INIT)
```

### Phandle Navigation

```c
/* Get node from phandle property */
#define PARENT_CTRL DT_PHANDLE(MY_NODE, parent_controller)

/* Phandle array navigation */
DT_PHANDLE_BY_IDX(node_id, prop, idx)
DT_PHANDLE_BY_NAME(node_id, prop, name)

/* Check if phandle exists */
DT_NODE_HAS_PROP(MY_NODE, optional_phandle)
```

### Tips

1. **Use _dt_spec structs** — `gpio_dt_spec`, `i2c_dt_spec`, etc. are cleaner than raw macros
2. **FOREACH for arrays** — More maintainable than manual indexing
3. **DT_INST_* in drivers** — Enables multi-instance drivers automatically
4. **Check generated header** — See `devicetree_generated.h` for actual macro expansions
5. **Static initialization** — All DT_* macros are compile-time constants

## Bindings

Bindings are YAML files that describe the requirements for devicetree nodes. Zephyr uses them to validate DTS files and generate C macros.

### Binding Structure

```yaml
compatible: "vendor,device"
description: "High-level description of the device"

include: [base.yaml, uart-controller.yaml]

properties:
  reg:
    type: array
    required: true
    description: MMIO register space
  current-speed:
    type: int
    default: 115200
  hw-flow-control:
    type: boolean

# If this node is a bus (e.g., I2C controller)
bus: i2c

# If this node is on a bus (e.g., I2C sensor)
on-bus: i2c

# Naming cells for phandle-arrays (e.g., #gpio-cells)
gpio-cells:
  - pin
  - flags
```

### Property Types in Bindings

- `string`, `int`, `boolean`, `array`, `uint8-array`, `string-array`
- `phandle`, `phandles`, `phandle-array`
- `path`: Path to a node (string or phandle reference)
- `compound`: Complex types (no macros generated)

### Key Concepts

- **required**: If `true`, the build fails if the property is missing in the DTS.
- **default**: Value used if the property is missing in the DTS.
- **enum**: Limits property values to a fixed list.
- **bus / on-bus**: Used for matching bindings based on the hardware hierarchy. A sensor on an I2C bus will match a binding with `on-bus: i2c`.
- **child-binding**: Constrains the children of the node (e.g., for `gpio-leds`).
- **specifier-cells**: (e.g., `gpio-cells`, `pwm-cells`) Names the cells in a `phandle-array` so they can be accessed by name in C.

### Where to find bindings
Bindings are usually located in `dts/bindings/` within the Zephyr tree or a module.
- `base.yaml`: Common properties for all nodes.
- `gpio-controller.yaml`: Common for GPIO controllers.
- `i2c-device.yaml`: Common for I2C slaves.

## Clocks

Clock configuration describes clock sources, frequencies, and how peripherals connect to clocks.

### Overview

The clock model has two parts:
1. **Clock providers** — Clock controllers that generate/distribute clocks
2. **Clock consumers** — Devices that use clocks from providers

### Clock Provider

Clock providers use `#clock-cells` to define how many cells identify each clock output.

```devicetree
rcc: rcc@40023800 {
    compatible = "st,stm32-rcc";
    reg = <0x40023800 0x400>;
    #clock-cells = <2>;  /* Two cells: bus-id and clock-id */
};

/* Or simpler single-cell provider */
clock: clock-controller@4000 {
    compatible = "vendor,clock-controller";
    reg = <0x4000 0x100>;
    #clock-cells = <1>;  /* One cell: clock-id */
};
```

### Clock Consumer

Consumers reference clocks using `clocks` and optionally `clock-names`:

```devicetree
&usart1 {
    clocks = <&rcc STM32_CLOCK_BUS_APB2 0x00000010>;
    /* Or with multiple clocks: */
    clocks = <&rcc CLOCK_PCLK>, <&rcc CLOCK_HSE>;
    clock-names = "apb", "hse";
};
```

### Properties

#### Provider Properties

| Property | Type | Description |
|----------|------|-------------|
| `#clock-cells` | int | Number of cells to identify a clock output |
| `clock-output-names` | string-array | Optional names for clock outputs |
| `clock-frequency` | int | Fixed clock frequency in Hz |

#### Consumer Properties

| Property | Type | Description |
|----------|------|-------------|
| `clocks` | phandle-array | Reference to clock(s) with specifier cells |
| `clock-names` | string-array | Names for each clock reference |
| `clock-frequency` | int | Override/specify frequency |
| `assigned-clocks` | phandle-array | Clocks to configure at boot |
| `assigned-clock-rates` | array | Rates for assigned-clocks |

### Vendor Patterns

#### STM32
```devicetree
/* STM32 uses two cells: bus and bit position */
&usart2 {
    clocks = <&rcc STM32_CLOCK_BUS_APB1 0x00020000>;
};
```

#### Nordic nRF
```devicetree
/* Nordic peripherals often don't need explicit clock config */
/* But for external oscillators: */
&clock {
    status = "okay";
    hfclk = "external";
};
```

#### NXP
```devicetree
&lpuart1 {
    clocks = <&ccm IMX_CCM_LPUART_CLK 0x7C 24>;
};
```

### Common Clock Types

```devicetree
/ {
    clocks {
        /* Fixed clock (e.g., external crystal) */
        xtal: xtal {
            compatible = "fixed-clock";
            #clock-cells = <0>;
            clock-frequency = <32768>;
        };

        /* Fixed-factor divider */
        pll_div2: pll-div2 {
            compatible = "fixed-factor-clock";
            #clock-cells = <0>;
            clocks = <&pll>;
            clock-mult = <1>;
            clock-div = <2>;
        };
    };
};
```

### Complete Example

```devicetree
/* Add external 8MHz crystal and use it for a peripheral */

/ {
    clocks {
        ext_osc: ext-osc {
            compatible = "fixed-clock";
            #clock-cells = <0>;
            clock-frequency = <8000000>;
        };
    };
};

&spi1 {
    status = "okay";
    clocks = <&rcc STM32_CLOCK_BUS_APB2 0x00001000>,
             <&ext_osc>;
    clock-names = "apb", "ext";
};
```

### C API

```c
#include <zephyr/drivers/clock_control.h>

/* Get clock rate from devicetree */
#define MY_NODE DT_NODELABEL(usart1)

/* Check if clocks property exists */
#if DT_NODE_HAS_PROP(MY_NODE, clocks)
    /* Get clock controller device */
    const struct device *clk = DEVICE_DT_GET(DT_CLOCKS_CTLR(MY_NODE));

    /* Get clock rate */
    uint32_t rate;
    clock_control_get_rate(clk,
                           (clock_control_subsys_t)DT_CLOCKS_CELL(MY_NODE, id),
                           &rate);
#endif

/* Macros for clock access */
DT_CLOCKS_CTLR(node_id)              /* Clock controller phandle */
DT_CLOCKS_CTLR_BY_IDX(node_id, idx)  /* Nth clock controller */
DT_CLOCKS_CTLR_BY_NAME(node_id, name) /* Clock by name */
DT_CLOCKS_CELL(node_id, cell)        /* Specific cell value */
DT_CLOCKS_CELL_BY_IDX(node_id, idx, cell)
DT_CLOCKS_CELL_BY_NAME(node_id, name, cell)
```

### Tips

1. **Check SoC dtsi** — Clock trees are pre-defined in SoC files
2. **Use clock-names** — Makes code more readable when multiple clocks exist
3. **Fixed clocks** — Use `fixed-clock` for external oscillators
4. **Build output** — Check `zephyr.dts` to verify clock assignments

## Debugging

Common errors, debugging techniques, and resolution strategies for devicetree issues.

### Key Build Outputs

After building, examine these files:

| File | Purpose |
|------|---------|
| `build/zephyr/zephyr.dts` | Final merged devicetree (overlays applied) |
| `build/zephyr/zephyr.dts.pre` | Pre-processed DTS (includes resolved) |
| `build/zephyr/include/generated/devicetree_generated.h` | Generated C macros |
| `build/zephyr/dts.cmake` | CMake devicetree variables |

### Common Errors and Fixes

#### 1. Binding Not Found

**Error:**
```
devicetree error: no binding for /soc/my-device@40000000
```

**Causes:**
- Missing `compatible` property
- `compatible` doesn't match any binding file
- Binding file not in search path

**Fix:**
```devicetree
/* Add or correct compatible */
my_device: my-device@40000000 {
    compatible = "vendor,my-device";  /* Must match binding filename */
    reg = <0x40000000 0x1000>;
};
```

Check binding exists at: `dts/bindings/*/vendor,my-device.yaml`

#### 2. Required Property Missing

**Error:**
```
devicetree error: 'reg' is required by binding but not in node /soc/uart@40000000
```

**Fix:**
Add the required property:
```devicetree
&uart0 {
    reg = <0x40000000 0x1000>;  /* Add required property */
};
```

#### 3. Property Type Mismatch

**Error:**
```
devicetree error: expected int for 'current-speed', got string
```

**Fix:**
```devicetree
/* Wrong */
current-speed = "115200";

/* Correct */
current-speed = <115200>;
```

#### 4. DEVICE_DT_GET Returns NULL

**Symptom:** Device pointer is NULL at runtime.

**Causes:**
1. Node has `status = "disabled"` or missing status
2. Driver not enabled in Kconfig
3. Wrong node identifier

**Debug:**
```c
const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(my_device));
if (!device_is_ready(dev)) {
    printk("Device not ready! Status: %d\n", dev->state->init_res);
}
```

**Fix:**
```devicetree
&my_device {
    status = "okay";  /* Enable the node */
};
```

Also check Kconfig:
```
CONFIG_MY_DRIVER=y
```

#### 5. Linker Error: __device_dts_ord_N

**Error:**
```
undefined reference to `__device_dts_ord_42'
```

**Cause:** Node exists in DTS but driver isn't compiled.

**Fix:**
Enable the driver in `prj.conf`:
```
CONFIG_UART_DRIVER=y
CONFIG_UART_VENDOR=y
```

#### 6. Duplicate Node or Property

**Error:**
```
devicetree error: duplicate node name 'uart@40000000'
```

**Fix:**
Use node label to modify existing node instead of creating new:
```devicetree
/* Wrong - creates duplicate */
/ {
    soc {
        uart@40000000 { ... };
    };
};

/* Correct - modifies existing */
&uart0 {
    status = "okay";
};
```

#### 7. Phandle Resolution Failed

**Error:**
```
devicetree error: phandle reference '&nonexistent' not found
```

**Fix:**
Verify the referenced node exists and has a label:
```devicetree
/* Ensure label exists */
gpio0: gpio@50000000 {
    ...
};

/* Then reference works */
my_device {
    gpios = <&gpio0 5 0>;
};
```

#### 8. Cell Count Mismatch

**Error:**
```
devicetree error: wrong number of cells for 'gpios' (got 2, expected 3)
```

**Fix:**
Check parent's `#*-cells` and provide correct number:
```devicetree
/* If gpio0 has #gpio-cells = <2> */
gpios = <&gpio0 5 GPIO_ACTIVE_LOW>;  /* 2 cells after phandle */

/* If has #gpio-cells = <3> */
gpios = <&gpio0 0 5 GPIO_ACTIVE_LOW>;  /* 3 cells after phandle */
```

### Debugging Commands

#### View Final Devicetree

```bash
# After build, view merged DTS
cat build/zephyr/zephyr.dts

# Or use west
west build -t zephyr.dts
```

#### Check Generated Macros

```bash
# Search for your node's macros
grep -r "my_device" build/zephyr/include/generated/devicetree_generated.h
```

#### Validate Devicetree Manually

```bash
# Run dtc directly for detailed errors
dtc -I dts -O dtb -o /dev/null build/zephyr/zephyr.dts.pre 2>&1
```

#### CMake Devicetree Info

```bash
# Show devicetree CMake variables
west build -t devicetree_info
```

### Overlay Debugging

#### Verify Overlay Applied

1. Build with overlay
2. Check `build/zephyr/zephyr.dts` for your changes
3. If changes missing, verify overlay path:

```bash
# Explicit overlay
west build -b my_board -- -DDTC_OVERLAY_FILE=my.overlay

# Check what overlays were used
grep DTC_OVERLAY build/CMakeCache.txt
```

#### Overlay Search Order

Zephyr looks for overlays in this order:
1. `DTC_OVERLAY_FILE` CMake variable
2. `boards/<BOARD>.overlay` in app directory
3. `app.overlay` in app directory

### Macro Debugging

#### Print Macro Values

```c
/* Stringify macro for debugging */
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define MY_NODE DT_NODELABEL(uart0)
#pragma message "REG_ADDR = " TOSTRING(DT_REG_ADDR(MY_NODE))
```

#### Check Node Existence at Compile Time

```c
#if !DT_NODE_EXISTS(DT_NODELABEL(my_device))
#error "Required node my_device not found in devicetree"
#endif

BUILD_ASSERT(DT_NODE_HAS_STATUS(DT_NODELABEL(uart0), okay),
             "uart0 must be enabled");
```

### Common Pitfalls

#### 1. Wrong Node Label Syntax

```devicetree
/* Wrong - using & in definition */
&my_label: node@1000 { };

/* Correct */
my_label: node@1000 { };

/* Reference uses & */
&my_label { status = "okay"; };
```

#### 2. Forgetting Semicolons

```devicetree
/* Wrong */
node {
    property = <1>   /* Missing semicolon */
}

/* Correct */
node {
    property = <1>;
};
```

#### 3. Case Sensitivity

Node names and properties are case-sensitive:
```devicetree
/* These are different nodes */
uart0: UART@40000000 { };
uart1: uart@40000000 { };
```

#### 4. Unit Address Mismatch

```devicetree
/* Wrong - unit address doesn't match reg */
uart@40000000 {
    reg = <0x50000000 0x1000>;
};

/* Correct */
uart@50000000 {
    reg = <0x50000000 0x1000>;
};
```

### Kconfig Integration Issues

#### Driver Not Found for Compatible

The compatible must match both:
1. A binding file
2. A driver's `DT_DRV_COMPAT`

```c
/* In driver */
#define DT_DRV_COMPAT vendor_my_device  /* Must match compatible */
```

#### Check Driver is Compiled

```bash
# See if driver object exists
ls build/zephyr/drivers/*/my_driver.c.obj
```

### Tips

1. **Start simple** — Test with minimal overlay, add complexity gradually
2. **Check zephyr.dts first** — Most issues visible in merged output
3. **Use node labels** — Prefer `DT_NODELABEL()` over `DT_PATH()`
4. **Read binding files** — They document required properties
5. **Enable verbose build** — `west build -v` shows DTC commands

## Dma

DMA (Direct Memory Access) configuration describes DMA controllers and how peripherals connect to DMA channels.

### Overview

The DMA model has two parts:
1. **DMA controllers** — Hardware that performs memory transfers
2. **DMA consumers** — Peripherals that use DMA channels

### DMA Controller

Controllers are marked with `dma-controller` and define cell format with `#dma-cells`:

```devicetree
dma1: dma@40026000 {
    compatible = "st,stm32-dma-v2";
    reg = <0x40026000 0x400>;
    interrupts = <11 0>, <12 0>, <13 0>, <14 0>,
                 <15 0>, <16 0>, <17 0>, <18 0>;
    dma-controller;
    #dma-cells = <4>;  /* channel, slot, config, features */
    status = "okay";
};
```

### DMA Consumer

Devices reference DMA channels using `dmas` and `dma-names`:

```devicetree
&spi1 {
    status = "okay";
    dmas = <&dma1 3 3 0x28440 0x03>,
           <&dma1 0 3 0x28480 0x03>;
    dma-names = "tx", "rx";
};
```

### Properties

#### Controller Properties

| Property | Type | Description |
|----------|------|-------------|
| `dma-controller` | boolean | Marks node as DMA controller |
| `#dma-cells` | int | Number of cells per DMA specifier |
| `dma-channels` | int | Number of DMA channels available |
| `dma-requests` | int | Number of DMA request lines |

#### Consumer Properties

| Property | Type | Description |
|----------|------|-------------|
| `dmas` | phandle-array | Reference to DMA channel(s) with specifier |
| `dma-names` | string-array | Names for each DMA reference (typically "tx", "rx") |

### Vendor Cell Formats

#### STM32 DMA - 4 cells
```
<&dma channel slot config features>
```
- `channel`: DMA stream/channel number
- `slot`: Request slot (mux selection)
- `config`: Configuration bits (direction, width, etc.)
- `features`: Feature flags

```devicetree
/* STM32 UART with DMA */
&usart1 {
    dmas = <&dma2 7 4 0x28440 0x03>,   /* TX: stream 7, slot 4 */
           <&dma2 2 4 0x28480 0x03>;   /* RX: stream 2, slot 4 */
    dma-names = "tx", "rx";
};
```

#### Nordic nRF - Variable cells
```
<&dma channel>
```

#### NXP EDMA - 2 cells
```
<&edma channel mux>
```

### DMAMUX (DMA Multiplexer)

Some SoCs have a DMA request multiplexer:

```devicetree
dmamux1: dmamux@40020800 {
    compatible = "st,stm32-dmamux";
    reg = <0x40020800 0x400>;
    #dma-cells = <3>;
    dma-channels = <16>;
    dma-generators = <4>;
    dma-requests = <107>;
};

&uart4 {
    dmas = <&dmamux1 0 52 (STM32_DMA_PERIPH_TX | STM32_DMA_MEM_INC)>,
           <&dmamux1 1 53 (STM32_DMA_PERIPH_RX | STM32_DMA_MEM_INC)>;
    dma-names = "tx", "rx";
};
```

### Complete Example

```devicetree
/* Enable DMA for SPI */
&dma1 {
    status = "okay";
};

&spi2 {
    status = "okay";
    pinctrl-0 = <&spi2_default>;
    pinctrl-names = "default";
    cs-gpios = <&gpio0 25 GPIO_ACTIVE_LOW>;

    dmas = <&dma1 4 3 (STM32_DMA_PERIPH_TX | STM32_DMA_MEM_INC | STM32_DMA_MEM_8BITS)>,
           <&dma1 3 3 (STM32_DMA_PERIPH_RX | STM32_DMA_MEM_INC | STM32_DMA_MEM_8BITS)>;
    dma-names = "tx", "rx";
};
```

### C API

DMA is typically configured automatically by drivers using devicetree. Manual access:

```c
#include <zephyr/drivers/dma.h>

#define MY_NODE DT_NODELABEL(spi2)

/* Check if DMA is configured */
#if DT_NODE_HAS_PROP(MY_NODE, dmas)
    /* Get DMA controller device */
    const struct device *dma_dev = DEVICE_DT_GET(DT_DMAS_CTLR(MY_NODE));

    /* Get channel from devicetree */
    uint32_t channel = DT_DMAS_CELL_BY_NAME(MY_NODE, tx, channel);
#endif

/* DMA macros */
DT_DMAS_CTLR(node_id)                    /* DMA controller phandle */
DT_DMAS_CTLR_BY_IDX(node_id, idx)        /* Nth DMA controller */
DT_DMAS_CTLR_BY_NAME(node_id, name)      /* DMA by name */
DT_DMAS_CELL_BY_NAME(node_id, name, cell) /* Specific cell value */
DT_DMAS_CELL_BY_IDX(node_id, idx, cell)
DT_HAS_DMA_CHANNEL(node_id, name)        /* Check if channel exists */
```

### Binding Example

```yaml
# dts/bindings/dma/vendor,my-dma.yaml
compatible: "vendor,my-dma"
include: [dma-controller.yaml]

properties:
  reg:
    required: true
  interrupts:
    required: true
  "#dma-cells":
    const: 2

dma-cells:
  - channel
  - config
```

### Tips

1. **Check SoC reference** — DMA slot/channel assignments are SoC-specific
2. **Enable controller** — DMA controller must have `status = "okay"`
3. **Interrupt per channel** — Each DMA channel typically needs an interrupt
4. **Driver support** — Not all drivers support DMA; check driver Kconfig
5. **Power consumption** — DMA can reduce CPU load but may affect power states

## Examples

Complete working examples for common hardware configurations.

---

### 1. GPIO LED (Blinky)

**DTS/Overlay:**
```devicetree
/ {
    leds {
        compatible = "gpio-leds";
        led0: led_0 {
            gpios = <&gpio0 13 GPIO_ACTIVE_LOW>;
            label = "Green LED";
        };
    };
    aliases {
        led0 = &led0;
    };
};
```

**C Code:**
```c
#include <zephyr/drivers/gpio.h>

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

int main(void) {
    if (!gpio_is_ready_dt(&led)) {
        return -ENODEV;
    }
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set_dt(&led, 1);
    return 0;
}
```

---

### 2. I2C Sensor

**DTS/Overlay:**
```devicetree
&i2c1 {
    status = "okay";
    clock-frequency = <I2C_BITRATE_FAST>;

    bme280: bme280@76 {
        compatible = "bosch,bme280";
        reg = <0x76>;
    };
};
```

**C Code:**
```c
#include <zephyr/drivers/sensor.h>

#define BME280_NODE DT_NODELABEL(bme280)
const struct device *const sensor = DEVICE_DT_GET(BME280_NODE);

int main(void) {
    if (!device_is_ready(sensor)) {
        return -ENODEV;
    }
    struct sensor_value temp;
    sensor_sample_fetch(sensor);
    sensor_channel_get(sensor, SENSOR_CHAN_AMBIENT_TEMP, &temp);
    printk("Temp: %d.%06d\n", temp.val1, temp.val2);
    return 0;
}
```

---

### 3. SPI Flash with Partitions

**DTS/Overlay:**
```devicetree
&spi1 {
    status = "okay";
    cs-gpios = <&gpio0 12 GPIO_ACTIVE_LOW>;

    mx25r64: flash@0 {
        compatible = "jedec,spi-nor";
        reg = <0>;
        spi-max-frequency = <80000000>;
        jedec-id = [c2 28 17];
        size = <67108864>;  /* 64 Mbit = 8 MB */

        partitions {
            compatible = "fixed-partitions";
            #address-cells = <1>;
            #size-cells = <1>;

            storage_partition: partition@0 {
                label = "storage";
                reg = <0x00000000 0x00100000>;  /* 1 MB */
            };
            littlefs_partition: partition@100000 {
                label = "littlefs";
                reg = <0x00100000 0x00700000>;  /* 7 MB */
            };
        };
    };
};

/ {
    chosen {
        zephyr,flash = &mx25r64;
    };
};
```

---

### 4. PWM Buzzer

**DTS/Overlay:**
```devicetree
/ {
    pwm_buzzer {
        compatible = "pwm-leds";
        buzzer: buzzer_0 {
            pwms = <&pwm0 2 1000000 PWM_POLARITY_NORMAL>;  /* 1ms period */
            label = "Buzzer";
        };
    };
};
```

**C Code:**
```c
#include <zephyr/drivers/pwm.h>

#define BUZZER_NODE DT_NODELABEL(buzzer)
static const struct pwm_dt_spec buzzer = PWM_DT_SPEC_GET(BUZZER_NODE);

void beep(uint32_t frequency_hz, uint32_t duration_ms) {
    uint32_t period = 1000000000U / frequency_hz;  /* ns */
    pwm_set_dt(&buzzer, period, period / 2);       /* 50% duty */
    k_msleep(duration_ms);
    pwm_set_pulse_dt(&buzzer, 0);                  /* Off */
}
```

---

### 5. UART with Pin Control

**DTS/Overlay (Nordic nRF):**
```devicetree
&pinctrl {
    uart1_default: uart1_default {
        group1 {
            psels = <NRF_PSEL(UART_TX, 1, 1)>,
                    <NRF_PSEL(UART_RX, 1, 2)>;
        };
    };
    uart1_sleep: uart1_sleep {
        group1 {
            psels = <NRF_PSEL(UART_TX, 1, 1)>,
                    <NRF_PSEL(UART_RX, 1, 2)>;
            low-power-enable;
        };
    };
};

&uart1 {
    status = "okay";
    current-speed = <115200>;
    pinctrl-0 = <&uart1_default>;
    pinctrl-1 = <&uart1_sleep>;
    pinctrl-names = "default", "sleep";
};
```

**C Code:**
```c
#include <zephyr/drivers/uart.h>

#define UART1_NODE DT_NODELABEL(uart1)
const struct device *const uart = DEVICE_DT_GET(UART1_NODE);

int main(void) {
    if (!device_is_ready(uart)) {
        return -ENODEV;
    }
    uart_poll_out(uart, 'H');
    uart_poll_out(uart, 'i');
    return 0;
}
```

---

### 6. ADC Channel Configuration

**DTS/Overlay:**
```devicetree
/ {
    zephyr,user {
        io-channels = <&adc0 0>, <&adc0 1>;
        io-channel-names = "voltage", "current";
    };
};

&adc0 {
    status = "okay";
    #address-cells = <1>;
    #size-cells = <0>;

    channel@0 {
        reg = <0>;
        zephyr,gain = "ADC_GAIN_1_6";
        zephyr,reference = "ADC_REF_INTERNAL";
        zephyr,acquisition-time = <ADC_ACQ_TIME_DEFAULT>;
        zephyr,resolution = <12>;
        zephyr,input-positive = <NRF_SAADC_AIN0>;
    };

    channel@1 {
        reg = <1>;
        zephyr,gain = "ADC_GAIN_1_6";
        zephyr,reference = "ADC_REF_INTERNAL";
        zephyr,acquisition-time = <ADC_ACQ_TIME_DEFAULT>;
        zephyr,resolution = <12>;
        zephyr,input-positive = <NRF_SAADC_AIN1>;
    };
};
```

**C Code:**
```c
#include <zephyr/drivers/adc.h>

#define ADC_NODE DT_NODELABEL(adc0)
#define ADC_CHANNEL_0 DT_CHILD(ADC_NODE, channel_0)

static const struct adc_dt_spec adc_voltage = ADC_DT_SPEC_GET(ADC_CHANNEL_0);

int read_voltage(void) {
    int16_t buf;
    struct adc_sequence seq = {
        .buffer = &buf,
        .buffer_size = sizeof(buf),
    };
    adc_sequence_init_dt(&adc_voltage, &seq);
    adc_read_dt(&adc_voltage, &seq);
    return buf;
}
```

---

### 7. CAN Bus Setup

**DTS/Overlay:**
```devicetree
&can1 {
    status = "okay";
    pinctrl-0 = <&can1_default>;
    pinctrl-names = "default";
    bitrate = <500000>;
    sample-point = <875>;
};
```

**C Code:**
```c
#include <zephyr/drivers/can.h>

#define CAN_NODE DT_NODELABEL(can1)
const struct device *const can = DEVICE_DT_GET(CAN_NODE);

int main(void) {
    if (!device_is_ready(can)) {
        return -ENODEV;
    }
    can_start(can);

    struct can_frame frame = {
        .id = 0x123,
        .dlc = 8,
        .data = {1, 2, 3, 4, 5, 6, 7, 8},
    };
    can_send(can, &frame, K_MSEC(100), NULL, NULL);
    return 0;
}
```

---

### 8. Timer/Counter Configuration

**DTS/Overlay:**
```devicetree
&timer0 {
    status = "okay";
};

/ {
    chosen {
        zephyr,counter = &timer0;
    };
};
```

**C Code:**
```c
#include <zephyr/drivers/counter.h>

#define COUNTER_NODE DT_CHOSEN(zephyr_counter)
const struct device *const counter = DEVICE_DT_GET(COUNTER_NODE);

void alarm_callback(const struct device *dev, uint8_t chan,
                    uint32_t ticks, void *user_data) {
    printk("Alarm fired!\n");
}

int main(void) {
    if (!device_is_ready(counter)) {
        return -ENODEV;
    }

    struct counter_alarm_cfg alarm = {
        .callback = alarm_callback,
        .ticks = counter_us_to_ticks(counter, 1000000),  /* 1 second */
        .flags = 0,
    };

    counter_start(counter);
    counter_set_channel_alarm(counter, 0, &alarm);
    return 0;
}
```

---

### 9. GPIO Keys (Buttons)

**DTS/Overlay:**
```devicetree
/ {
    buttons {
        compatible = "gpio-keys";
        button0: button_0 {
            gpios = <&gpio0 11 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
            label = "User Button";
            zephyr,code = <INPUT_KEY_0>;
        };
    };

    aliases {
        sw0 = &button0;
    };
};
```

**C Code:**
```c
#include <zephyr/drivers/gpio.h>

#define SW0_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);
static struct gpio_callback button_cb_data;

void button_pressed(const struct device *dev, struct gpio_callback *cb,
                    uint32_t pins) {
    printk("Button pressed!\n");
}

int main(void) {
    gpio_pin_configure_dt(&button, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);
    return 0;
}
```

---

### 10. Display (SPI)

**DTS/Overlay:**
```devicetree
&spi2 {
    status = "okay";
    cs-gpios = <&gpio0 25 GPIO_ACTIVE_LOW>;

    st7789v: st7789v@0 {
        compatible = "sitronix,st7789v";
        reg = <0>;
        spi-max-frequency = <20000000>;
        cmd-data-gpios = <&gpio0 24 GPIO_ACTIVE_LOW>;
        reset-gpios = <&gpio0 23 GPIO_ACTIVE_LOW>;
        width = <240>;
        height = <320>;
        x-offset = <0>;
        y-offset = <0>;
        vcom = <0x19>;
        gctrl = <0x35>;
        vrhs = <0x12>;
        vdvs = <0x20>;
        mdac = <0x00>;
        gamma = <0x01>;
        colmod = <0x05>;
        lcm = <0x2c>;
        porch-param = [0c 0c 00 33 33];
        cmd2en-param = [5a 69 02 01];
        pwctrl1-param = [a4 a1];
        pvgam-param = [D0 04 0D 11 13 2B 3F 54 4C 18 0D 0B 1F 23];
        nvgam-param = [D0 04 0C 11 13 2C 3F 44 51 2F 1F 1F 20 23];
        ram-param = [00 F0];
        rgb-param = [CD 08 14];
    };
};

/ {
    chosen {
        zephyr,display = &st7789v;
    };
};
```

---

### 11. External Interrupt (EXTI)

**DTS/Overlay:**
```devicetree
/ {
    gpio_keys {
        compatible = "gpio-keys";
        motion_sensor: motion_sensor {
            gpios = <&gpio0 7 (GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN)>;
            label = "Motion Sensor INT";
        };
    };
};
```

**C Code:**
```c
#define MOTION_NODE DT_NODELABEL(motion_sensor)
static const struct gpio_dt_spec motion = GPIO_DT_SPEC_GET(MOTION_NODE, gpios);

void motion_detected(const struct device *dev, struct gpio_callback *cb,
                     uint32_t pins) {
    printk("Motion detected!\n");
}
```

---

### 12. Watchdog Timer

**DTS/Overlay:**
```devicetree
&wdt0 {
    status = "okay";
};
```

**C Code:**
```c
#include <zephyr/drivers/watchdog.h>

#define WDT_NODE DT_NODELABEL(wdt0)
const struct device *const wdt = DEVICE_DT_GET(WDT_NODE);

int main(void) {
    struct wdt_timeout_cfg cfg = {
        .window.min = 0,
        .window.max = 5000,  /* 5 seconds */
        .callback = NULL,
        .flags = WDT_FLAG_RESET_SOC,
    };

    int wdt_channel = wdt_install_timeout(wdt, &cfg);
    wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);

    while (1) {
        wdt_feed(wdt, wdt_channel);
        k_msleep(1000);
    }
}

## Interrupts

Interrupt configuration describes how devices signal events to the CPU via interrupt controllers.

### Overview

The interrupt model has:
1. **Interrupt controllers** — Handle and route interrupts (NVIC, GIC, etc.)
2. **Interrupt consumers** — Devices that generate interrupts
3. **Interrupt nexus** — Optional routing/mapping layer

### Interrupt Controller

Controllers are marked with `interrupt-controller` and define cell format with `#interrupt-cells`:

```devicetree
nvic: interrupt-controller@e000e100 {
    compatible = "arm,v7m-nvic";
    reg = <0xe000e100 0xc00>;
    interrupt-controller;
    #interrupt-cells = <2>;  /* IRQ number + priority */
};

/* ARM GIC example */
gic: interrupt-controller@8000000 {
    compatible = "arm,gic-v3";
    interrupt-controller;
    #interrupt-cells = <3>;  /* Type + IRQ + flags */
};
```

### Interrupt Consumer

Devices reference interrupts and optionally their parent controller:

```devicetree
&uart0 {
    interrupts = <12 1>;  /* IRQ 12, priority 1 */
    interrupt-parent = <&nvic>;
};

/* Multiple interrupts */
&dma1 {
    interrupts = <11 0>, <12 0>, <13 0>;
    interrupt-names = "chan0", "chan1", "chan2";
};
```

### Properties

#### Controller Properties

| Property | Type | Description |
|----------|------|-------------|
| `interrupt-controller` | boolean | Marks node as interrupt controller |
| `#interrupt-cells` | int | Number of cells per interrupt specifier |

#### Consumer Properties

| Property | Type | Description |
|----------|------|-------------|
| `interrupts` | array | Interrupt specifier(s) |
| `interrupt-parent` | phandle | Override default interrupt controller |
| `interrupt-names` | string-array | Names for each interrupt |
| `interrupts-extended` | phandle-array | Mix different controllers |

### Cell Formats

#### ARM Cortex-M (NVIC) - 2 cells
```
<irq_number priority>
```
- `irq_number`: Interrupt vector number
- `priority`: Interrupt priority (0 = highest)

#### ARM GIC - 3 cells
```
<type irq_number flags>
```
- `type`: 0 = SPI (shared), 1 = PPI (private)
- `irq_number`: Interrupt ID
- `flags`: Trigger type (edge/level, polarity)

#### RISC-V PLIC - 2 cells
```
<irq_number priority>
```

### Default Interrupt Parent

Set a default controller for all children:

```devicetree
soc {
    interrupt-parent = <&nvic>;

    uart0: uart@40000000 {
        /* Uses &nvic automatically */
        interrupts = <12 1>;
    };
};
```

### Multiple Interrupt Controllers

Use `interrupts-extended` to reference different controllers:

```devicetree
my_device {
    interrupts-extended = <&nvic 5 1>,
                          <&gpio0 3 (GPIO_INT_EDGE | GPIO_INT_ACTIVE_LOW)>;
    interrupt-names = "tx", "gpio-alert";
};
```

### Interrupt Nexus (Mapping)

For complex routing, use `interrupt-map`:

```devicetree
pcie: pcie@1000 {
    interrupt-map-mask = <0x1800 0 0 7>;
    interrupt-map = <0x0000 0 0 1 &gic 0 14 4>,
                    <0x0000 0 0 2 &gic 0 15 4>,
                    <0x0800 0 0 1 &gic 0 16 4>,
                    <0x0800 0 0 2 &gic 0 17 4>;
    #interrupt-cells = <1>;
};
```

### GPIO Interrupts

GPIO controllers often act as interrupt controllers:

```devicetree
gpio0: gpio@50000000 {
    compatible = "nordic,nrf-gpio";
    gpio-controller;
    #gpio-cells = <2>;
    interrupt-controller;
    #interrupt-cells = <2>;
};

button0 {
    gpios = <&gpio0 11 GPIO_ACTIVE_LOW>;
    /* GPIO interrupt via gpio-controller's interrupt-controller capability */
};
```

### Complete Example

```devicetree
/* Configure UART with interrupt */
&uart0 {
    status = "okay";
    current-speed = <115200>;
    interrupts = <2 1>;  /* IRQ 2, priority 1 */
    interrupt-names = "uart0";
};

/* External interrupt on GPIO */
/ {
    buttons {
        compatible = "gpio-keys";
        button0: button_0 {
            gpios = <&gpio0 11 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
            zephyr,code = <INPUT_KEY_0>;
        };
    };
};
```

### C API

```c
#include <zephyr/devicetree.h>

#define MY_NODE DT_NODELABEL(uart0)

/* Get interrupt number */
#define MY_IRQ DT_IRQN(MY_NODE)

/* Get interrupt priority */
#define MY_IRQ_PRIO DT_IRQ(MY_NODE, priority)

/* For multiple interrupts */
DT_IRQ_BY_IDX(node_id, idx, cell)     /* Nth interrupt's cell */
DT_IRQ_BY_NAME(node_id, name, cell)   /* Named interrupt's cell */
DT_NUM_IRQS(node_id)                  /* Count of interrupts */

/* In driver initialization */
IRQ_CONNECT(DT_IRQN(MY_NODE),
            DT_IRQ(MY_NODE, priority),
            my_isr,
            NULL,
            0);
irq_enable(DT_IRQN(MY_NODE));

/* Or using instance macros in drivers */
#define DT_DRV_COMPAT vendor_device
IRQ_CONNECT(DT_INST_IRQN(0),
            DT_INST_IRQ(0, priority),
            my_isr,
            DEVICE_DT_INST_GET(0),
            0);
```

### Tips

1. **Check SoC header** — IRQ numbers are often defined in SoC-specific headers
2. **Priority values** — Lower number = higher priority on most ARMs
3. **interrupt-parent inheritance** — Children inherit parent's interrupt-parent
4. **Shared interrupts** — Use `interrupt-names` to distinguish in code
5. **GPIO interrupts** — Typically configured via GPIO API, not raw IRQ

## Macros

Zephyr provides a comprehensive set of C macros in `<zephyr/devicetree.h>` to access devicetree data at build-time.

### Node Identifiers
Node identifiers are internal representations of nodes. They are NOT variables and cannot be stored.

- `DT_PATH(soc, uart_40002000)`: Get ID by full path (slashes replaced by underscores).
- `DT_NODELABEL(uart0)`: Get ID by node label. **(Preferred)**
- `DT_ALIAS(my_uart)`: Get ID via `/aliases` node.
- `DT_CHOSEN(zephyr_console)`: Get ID via `/chosen` node.
- `DT_INST(inst, compat)`: Get ID by instance number of a compatible. Used in drivers.
- `DT_PARENT(node_id)` / `DT_CHILD(node_id, child_name)`: Navigate hierarchy.

### Property Access
- `DT_PROP(node_id, prop)`: Get property value (int, string, bool).
- `DT_PROP_LEN(node_id, prop)`: Get length of an array property.
- `DT_ENUM_IDX(node_id, prop)`: Get index of an enum value.

### Register and Interrupt Access
- `DT_REG_ADDR(node_id)`: Base address of `reg`.
- `DT_REG_SIZE(node_id)`: Size of `reg`.
- `DT_REG_ADDR_BY_IDX(node_id, idx)`: Address of Nth register block.
- `DT_NUM_REGS(node_id)`: Total number of register blocks.
- `DT_IRQN(node_id)`: Get the interrupt number.
- `DT_IRQ_BY_IDX(node_id, idx, cell)`: Get a specific cell of the Nth interrupt.

### Phandle and Hardware APIs
- `DT_PHANDLE(node_id, prop)`: Get node ID from a phandle property.
- `DT_PHA(node_id, prop, cell)`: Get a cell value from a phandle array.
- `DT_GPIO_PIN(node_id, prop)`: Specific helper for GPIO pins.
- `DT_GPIO_FLAGS(node_id, prop)`: Specific helper for GPIO flags.

### Existence and Status Checks
- `DT_NODE_EXISTS(node_id)`: Check if node exists.
- `DT_NODE_HAS_STATUS(node_id, okay)`: Check if node is enabled.
- `DT_NODE_HAS_PROP(node_id, prop)`: Check if property exists.
- `DT_HAS_COMPAT_STATUS_OKAY(compat)`: Check if any node with compat is enabled.

### Driver Conveniences
When writing drivers, define `DT_DRV_COMPAT`:
```c
#define DT_DRV_COMPAT vendor_device
DT_INST_PROP(0, clock_frequency) // Access prop of instance 0
```

## Overlays

Overlays (.overlay files) allow you to modify the base devicetree without changing the original board files.

### Common Tasks

#### Overriding a Property
Use a node label to refer to the node and override its property.

```devicetree
&uart0 {
    current-speed = <9600>;
    status = "okay";
};
```

#### Adding an Alias or Chosen Node
```devicetree
/ {
    aliases {
        debug-uart = &uart0;
    };

    chosen {
        zephyr,console = &uart0;
    };
};
```

#### Deleting a Property or Node
```devicetree
&uart0 {
    /delete-property/ hw-flow-control;
};

/ {
    /delete-node/ unwanted-node;
};
```

#### Adding a Child Device (e.g., I2C Sensor)
```devicetree
&i2c1 {
    status = "okay";
    my_sensor: sensor@4a {
        compatible = "vendor,sensor-model";
        reg = <0x4a>;
        label = "ENV_SENSOR";
    };
};
```

### Overlay Search Order
If not explicitly set via `DTC_OVERLAY_FILE`, Zephyr looks for:
1. `socs/<SOC>_<BOARD_QUALIFIERS>.overlay`
2. `boards/<BOARD>.overlay`
3. `boards/<BOARD>_<revision>.overlay`
4. `<BOARD>.overlay`
5. `app.overlay`

## Pinctrl

Pin control configures pin multiplexing (which peripheral uses which pin) and electrical properties (pull-up, drive strength, etc.).

### Overview

Zephyr's pinctrl subsystem uses a two-part model:
1. **Pin configuration nodes** — Define specific pin states
2. **Device references** — Devices reference their pin configurations via `pinctrl-N` properties

### Basic Structure

#### Pin Configuration (Vendor-Specific)

Pin configurations are defined under a pinctrl node. The exact format varies by vendor.

**Nordic nRF:**
```devicetree
&pinctrl {
    uart0_default: uart0_default {
        group1 {
            psels = <NRF_PSEL(UART_TX, 0, 6)>,
                    <NRF_PSEL(UART_RX, 0, 8)>;
        };
    };
    uart0_sleep: uart0_sleep {
        group1 {
            psels = <NRF_PSEL(UART_TX, 0, 6)>,
                    <NRF_PSEL(UART_RX, 0, 8)>;
            low-power-enable;
        };
    };
};
```

**STM32:**
```devicetree
&pinctrl {
    usart2_tx_pa2: usart2_tx_pa2 {
        pinmux = <STM32_PINMUX('A', 2, AF7)>;
    };
    usart2_rx_pa3: usart2_rx_pa3 {
        pinmux = <STM32_PINMUX('A', 3, AF7)>;
    };
};
```

**NXP:**
```devicetree
&pinctrl {
    pinmux_lpuart1: pinmux_lpuart1 {
        group0 {
            pinmux = <&iomuxc_gpio_ad_24_lpuart1_txd>,
                     <&iomuxc_gpio_ad_25_lpuart1_rxd>;
            drive-strength = "high";
            slew-rate = "fast";
        };
    };
};
```

#### Device Referencing Pinctrl

Devices reference pin states using `pinctrl-N` and `pinctrl-names`:

```devicetree
&uart0 {
    status = "okay";
    current-speed = <115200>;
    pinctrl-0 = <&uart0_default>;
    pinctrl-1 = <&uart0_sleep>;
    pinctrl-names = "default", "sleep";
};
```

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `pinctrl-0` | phandle-array | First pin state (typically "default") |
| `pinctrl-1` | phandle-array | Second pin state (typically "sleep") |
| `pinctrl-N` | phandle-array | Nth pin state |
| `pinctrl-names` | string-array | Names for each state: `"default"`, `"sleep"`, etc. |

### Common Pin Properties

Electrical properties vary by vendor but common ones include:

| Property | Description |
|----------|-------------|
| `bias-disable` | No pull-up/pull-down |
| `bias-pull-up` | Enable pull-up resistor |
| `bias-pull-down` | Enable pull-down resistor |
| `drive-push-pull` | Push-pull drive mode |
| `drive-open-drain` | Open-drain drive mode |
| `input-enable` | Enable input buffer |
| `output-enable` | Enable output buffer |
| `low-power-enable` | Low power mode (Nordic) |
| `drive-strength` | Drive strength setting |
| `slew-rate` | Slew rate control |

### Vendor Macros

#### Nordic nRF
```c
NRF_PSEL(function, port, pin)
// Example: NRF_PSEL(UART_TX, 0, 6) = UART TX on P0.06
```

#### STM32
```c
STM32_PINMUX(port, pin, af)
// Example: STM32_PINMUX('A', 2, AF7) = PA2 with alternate function 7
```

#### NXP i.MX RT
Uses phandle references to pre-defined pin nodes in SoC dtsi files.

### Complete Overlay Example

```devicetree
/* app.overlay - Add UART with custom pinctrl */

&pinctrl {
    uart1_default: uart1_default {
        group1 {
            psels = <NRF_PSEL(UART_TX, 1, 1)>,
                    <NRF_PSEL(UART_RX, 1, 2)>;
        };
    };
};

&uart1 {
    status = "okay";
    current-speed = <115200>;
    pinctrl-0 = <&uart1_default>;
    pinctrl-names = "default";
};
```

### C API

```c
#include <zephyr/drivers/pinctrl.h>

/* Pinctrl is typically handled automatically by device drivers */
/* Manual control (rarely needed): */
PINCTRL_DT_DEFINE(DT_NODELABEL(uart0));
const struct pinctrl_dev_config *pcfg = PINCTRL_DT_DEV_CONFIG_GET(DT_NODELABEL(uart0));
pinctrl_apply_state(pcfg, PINCTRL_STATE_DEFAULT);
pinctrl_apply_state(pcfg, PINCTRL_STATE_SLEEP);
```

### Tips

1. **Check SoC dtsi** — Most SoCs pre-define pinctrl nodes; you often just reference them
2. **State names matter** — Use `"default"` and `"sleep"` for automatic power management
3. **Multiple groups** — Use multiple groups within a state for different electrical settings
4. **Binding files** — Vendor-specific properties are documented in `dts/bindings/pinctrl/` bindings

## Syntax

Devicetree is a hierarchical data structure used to describe hardware. In Zephyr, it's defined in Devicetree Source (DTS) files.

### Basic Structure

```devicetree
/dts-v1/;

/ {
    soc {
        serial0: uart@40002000 {
            compatible = "nordic,nrf-uarte";
            reg = <0x40002000 0x1000>;
            status = "okay";
        };
    };

    aliases {
        my-uart = &serial0;
    };

    chosen {
        zephyr,console = &serial0;
    };
};
```

- `/dts-v1/;`: Required version header.
- `/`: The root node.
- `nodes`: Defined as `name@unit-address { ... };`.
- `node labels`: Shorthands like `serial0:` used to refer to nodes elsewhere (e.g., `&serial0`).
- `properties`: Name/value pairs like `compatible = "vendor,device";`.

### Nodes

Nodes represent hardware components.
- **Path**: `/soc/uart@40002000` identifies the node's location.
- **Unit Address**: The part after `@` (e.g., `40002000`). It represents the node's address in its parent's address space (MMIO address, I2C address, SPI chip select, etc.).

### Important Properties

- **compatible**: A list of strings identifying the hardware. Used to match the node with a driver and binding. Format: `"vendor,model"`.
- **reg**: Address and length of the device's registers. Format: `<addr len>`.
- **status**: Use `"okay"` to enable a node or `"disabled"` to disable it.
- **interrupts**: Interrupt specifiers for the device.

### Property Value Types

| Type | Syntax | Example |
| :--- | :--- | :--- |
| `string` | Double quotes | `label = "my-device";` |
| `int` | Angle brackets | `foo = <1>;` |
| `boolean` | No value (present = true) | `hw-flow-control;` |
| `array` | Angle brackets, space-separated | `foo = <1 2 3>;` |
| `uint8-array` | Square brackets, hex | `mac = [01 02 03];` |
| `string-array` | Comma-separated strings | `names = "a", "b";` |
| `phandle` | Angle brackets with `&` | `irq-parent = <&intc>;` |
| `phandle-array`| List of phandles + cells | `pwms = <&pwm0 1 1000>;` |

### Special Nodes

- **aliases**: User-defined shorthands for nodes (e.g., `led0 = &led_red_node;`).
- **chosen**: System-wide configuration (e.g., `zephyr,console = &uart0;`).
