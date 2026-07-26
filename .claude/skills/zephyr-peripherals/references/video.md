# Video capture

Camera / video capture via Zephyr's video subsystem. Used in this workspace by
`applications/embedded_vision` (Arduino Nicla Vision, and an ESP32-P4-Nano
variant with a CSI camera).

## Header path changed in 4.5

```c
#include <zephyr/video/video.h>    /* current */
#include <zephyr/drivers/video.h>  /* pre-4.5 path — still works, it now just
                                    * #includes the above. No deprecation
                                    * warning, so stale includes are invisible. */
```

Supporting types (`struct video_format`, `enum video_buf_type`, FourCC macros)
live in `<zephyr/video/types.h>`, pulled in by `video.h`.

## Contents

1. [Capture loop](#capture)
2. [Formats and capabilities](#formats)
3. [Buffers](#buffers)
4. [Controls](#controls)
5. [Devicetree wiring](#devicetree)
6. [Kconfig](#kconfig)
7. [Traps](#traps)

## <a name="capture"></a>Capture loop

The model is a queue of buffers you hand to the driver and get back filled:
enqueue empty buffers, start the stream, dequeue filled ones, re-enqueue.

```c
#include <zephyr/video/video.h>

const struct device *const cam = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));

int capture(void)
{
    struct video_format fmt = {0};
    struct video_buffer *vbuf;
    int ret;

    if (!device_is_ready(cam)) {
        return -ENODEV;
    }

    /* 1. Pick a format */
    fmt.type = VIDEO_BUF_TYPE_OUTPUT;
    fmt.pixelformat = VIDEO_PIX_FMT_RGB565;
    fmt.width = 320;
    fmt.height = 240;
    fmt.pitch = fmt.width * 2;

    ret = video_set_format(cam, &fmt);
    if (ret) {
        return ret;
    }

    /* 2. Queue empty buffers before starting the stream */
    for (int i = 0; i < CONFIG_VIDEO_BUFFER_POOL_NUM_MAX; i++) {
        vbuf = video_buffer_alloc(fmt.pitch * fmt.height, K_NO_WAIT);
        if (vbuf == NULL) {
            return -ENOMEM;
        }
        vbuf->type = VIDEO_BUF_TYPE_OUTPUT;
        ret = video_enqueue(cam, vbuf);
        if (ret) {
            return ret;
        }
    }

    /* 3. Start */
    ret = video_stream_start(cam, VIDEO_BUF_TYPE_OUTPUT);
    if (ret) {
        return ret;
    }

    /* 4. Consume frames */
    while (1) {
        vbuf = &(struct video_buffer){ .type = VIDEO_BUF_TYPE_OUTPUT };

        ret = video_dequeue(cam, &vbuf, K_FOREVER);
        if (ret) {
            return ret;
        }

        process(vbuf->buffer, vbuf->bytesused);

        /* Always give it back, or the pool starves after N frames */
        ret = video_enqueue(cam, vbuf);
        if (ret) {
            return ret;
        }
    }
}
```

`video_stream_stop(cam, VIDEO_BUF_TYPE_OUTPUT)` halts capture. Buffers still
owned by the driver come back via `video_dequeue()`; release them with
`video_buffer_release()`.

### Event-driven instead of blocking

`video_dequeue()` with `K_FOREVER` blocks a thread. To multiplex several video
devices (e.g. camera → UVC), attach a poll signal and use `K_NO_WAIT`:

```c
struct k_poll_signal sig;
struct k_poll_event evt[1];

k_poll_signal_init(&sig);
video_set_signal(cam, &sig);
k_poll_event_init(&evt[0], K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig);

while (1) {
    k_poll(evt, ARRAY_SIZE(evt), K_FOREVER);
    k_poll_signal_reset(&sig);

    vbuf = &(struct video_buffer){ .type = VIDEO_BUF_TYPE_OUTPUT };
    if (video_dequeue(cam, &vbuf, K_NO_WAIT) == 0) {
        /* ... */
    }
}
```

`video_transfer_buffer(src, sink, ...)` moves a buffer between two video
devices without a copy — the camera-to-UVC path in `embedded_vision`.

## <a name="formats"></a>Formats and capabilities

Enumerate before assuming — sensors support a fixed, often small, set:

```c
struct video_caps caps = { .type = VIDEO_BUF_TYPE_OUTPUT };

video_get_caps(cam, &caps);
for (int i = 0; caps.format_caps[i].pixelformat != 0; i++) {
    const struct video_format_cap *c = &caps.format_caps[i];

    printk("%c%c%c%c %ux%u..%ux%u\n",
           (char)(c->pixelformat), (char)(c->pixelformat >> 8),
           (char)(c->pixelformat >> 16), (char)(c->pixelformat >> 24),
           c->width_min, c->height_min, c->width_max, c->height_max);
}
```

`caps.min_vbuf_count` is the minimum number of buffers you must enqueue before
`video_stream_start()`; queueing fewer can fail or drop every frame.

`struct video_format` fields: `type`, `pixelformat` (FourCC), `width`,
`height`, `pitch` (bytes per row, `>= width * bytes_per_pixel`).
`video_estimate_fmt_size(&fmt)` fills in the buffer size for a format.

Frame intervals (frame rate):

```c
struct video_frmival fi = { .numerator = 1, .denominator = 30 };  /* 30 fps */

video_set_frmival(cam, &fi);
video_get_frmival(cam, &fi);
```

Use `video_enum_frmival()` to list what's supported, and
`video_closest_frmival()` to snap a request to the nearest achievable rate
rather than failing.

Cropping and scaling use `video_set_selection()` / `video_get_selection()`, and
`video_set_compose_format()` for the output composition.

## <a name="buffers"></a>Buffers

| Call | Use |
|------|-----|
| `video_buffer_alloc(size, timeout)` | Allocate from the subsystem's pool |
| `video_import_buffer(mem, sz)` | Wrap memory you already own |
| `video_buffer_release(buf)` | Return to the pool |

**`video_import_buffer()` changed signature in 4.5.** It no longer takes a
`uint16_t *idx` out-parameter; it returns the `struct video_buffer *` directly
(or `NULL` on failure), so the buffer is reachable from application code
without tracking an index:

```c
/* 4.5+ */
struct video_buffer *vbuf = video_import_buffer(my_dma_mem, sizeof(my_dma_mem));

if (vbuf == NULL) {
    return -ENOMEM;
}
```

Importing is the right call when frames must land in a specific region — a
DMA-capable or PSRAM-backed buffer, or memory shared with an accelerator.

## <a name="controls"></a>Controls

```c
struct video_control ctrl = { .id = VIDEO_CID_EXPOSURE, .val = 200 };

video_set_ctrl(cam, &ctrl);
video_get_ctrl(cam, &ctrl);
```

Common IDs: `VIDEO_CID_BRIGHTNESS`, `VIDEO_CID_CONTRAST`, `VIDEO_CID_GAIN`,
`VIDEO_CID_EXPOSURE`, `VIDEO_CID_HFLIP`, `VIDEO_CID_VFLIP`,
`VIDEO_CID_TEST_PATTERN`. `video_query_ctrl()` reports range and default;
`video_print_ctrl()` dumps one for debugging. `-ENOTSUP` from a set means the
sensor driver doesn't implement that control, which is common.

`VIDEO_CID_TEST_PATTERN` is the fastest way to separate "sensor not streaming"
from "pipeline broken" — if the test pattern arrives, the capture path works and
the problem is upstream in the sensor's clock or configuration.

## <a name="devicetree"></a>Devicetree wiring

The camera is normally reached via a chosen node:

```dts
/ {
    chosen {
        zephyr,camera = &dvp_cam;
    };
};
```

The sensor hangs off its control bus (SCCB/I2C) and its data path is described
by the capture peripheral (DVP parallel, or MIPI CSI-2):

```dts
&i2c0 {
    status = "okay";

    imx219: imx219@10 {
        compatible = "sony,imx219";
        reg = <0x10>;
        status = "okay";

        port {
            imx219_ep_out: endpoint {
                remote-endpoint-label = "csi_ep_in";
            };
        };
    };
};
```

Sensor drivers connect to the capture controller through
`port`/`endpoint` nodes with `remote-endpoint-label` on both sides — a mismatch
here is a build error, which is the good case.

### CSI camera notes (ESP32-P4-Nano, verified on hardware)

- The Raspberry-Pi-style 15-pin CSI connector carries **no host XCLK**. An
  IMX219 module is self-clocked from its own oscillator, so do not expect to
  find or configure a clock output for it — a missing `clocks` property on the
  sensor is correct here, not an omission.
- SCCB (the sensor's I2C control channel) is on `i2c0`, GPIO7/GPIO8 on that
  board.
- There is no reset GPIO on that connector; omit `reset-gpios` rather than
  pointing it at an unused pin.
- Zephyr's console is on `uart0` via the CH343 USB-serial bridge, not USB-CDC.

## <a name="kconfig"></a>Kconfig

```kconfig
CONFIG_VIDEO=y
CONFIG_VIDEO_BUFFER_POOL_NUM_MAX=3          # max buffers in the pool
CONFIG_VIDEO_BUFFER_POOL_HEAP_SIZE=460800   # total pool bytes (3 * 320*240*2)
CONFIG_VIDEO_BUFFER_POOL_ALIGN=32           # DMA alignment, if the driver needs it

# Place the frame pool in a named linker region, or in external RAM
CONFIG_VIDEO_BUFFER_POOL_ZEPHYR_REGION=y
CONFIG_VIDEO_BUFFER_POOL_ZEPHYR_REGION_NAME="SRAM2"
CONFIG_VIDEO_BUFFER_USE_SHARED_MULTI_HEAP=y

# Debugging
CONFIG_VIDEO_LOG_LEVEL_DBG=y
CONFIG_VIDEO_SHELL=y                        # `video` shell commands
```

The pool is a **heap sized in total bytes** (`..._POOL_HEAP_SIZE`), not
per-buffer — size it for `num_buffers * bytes_per_frame` plus alignment
overhead. There is no `CONFIG_VIDEO_BUFFER_POOL_SZ_MAX`.

`CONFIG_VIDEO_SHELL` is worth enabling while bringing a camera up — it lists
devices, formats and controls without a rebuild cycle. See
`../../zephyr-debugging/references/probing.md`.

## <a name="traps"></a>Traps

- **A frame buffer rarely fits in SRAM.** 320x240 RGB565 is 150 KB; VGA is
  600 KB. Either shrink the format, or move the pool to external RAM. On ESP32
  targets that means PSRAM — and see the next trap before enabling it.
- **PSRAM can hang boot on ESP32-P4-Nano rev 1.3.** `CONFIG_ESP_SPIRAM=y` hangs
  before the boot banner on that engineering-sample silicon at both selectable
  speeds (isolated with `hello_world`). That blocks any PSRAM-backed capture
  path on this board — keep formats small enough for internal RAM instead. See
  `../../zephyr-system/references/sysbuild-mcuboot.md` for the related
  MCUboot limitation on the same board.
- **Enqueue before start.** `video_stream_start()` with an empty queue gives you
  a stream with nowhere to write; symptoms are zero dequeues rather than an
  error. Queue at least `caps.min_vbuf_count` buffers first.
- **Always re-enqueue after processing.** Forgetting to return the buffer works
  for the first N frames and then blocks forever — it looks like the sensor
  stopped, but the pool is simply empty.
- **`vbuf->bytesused`, not the buffer size**, is the valid byte count of a
  captured frame. Compressed formats (JPEG) vary per frame.
- **`pitch` is not always `width * bpp`.** Some controllers pad rows for
  alignment; trust the value returned by `video_get_format()` after the driver
  has adjusted your request, and re-read the format instead of assuming the one
  you set was accepted verbatim.
- **The pre-4.5 `<zephyr/drivers/video.h>` include still compiles** with no
  warning, so stale includes don't announce themselves. In this workspace
  `applications/embedded_vision/src/main.c` still uses the old path — harmless,
  but new code should use `<zephyr/video/video.h>`.
