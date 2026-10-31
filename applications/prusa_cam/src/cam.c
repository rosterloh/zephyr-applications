/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/video/video.h>

#include "cam.h"

LOG_MODULE_REGISTER(cam, LOG_LEVEL_INF);

#define NBUF          2
#define FRAME_TIMEOUT K_SECONDS(3)
/* Upstream caps the flash LED at 80 % to spare it. */
#define FLASH_PERCENT 80
#define FLASH_SETTLE  K_MSEC(150)

static const struct device *const video = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));
static const struct led_dt_spec flash_led = LED_DT_SPEC_GET(DT_NODELABEL(flash_led));

static struct video_buffer *bufs[NBUF];
static struct video_buffer *held;
static bool use_flash;
static K_MUTEX_DEFINE(cam_lock);

static int set_ctrl(uint32_t id, int32_t val)
{
	struct video_control ctrl = {.id = id, .val = val};
	int ret = video_set_ctrl(video, &ctrl);

	if (ret < 0 && ret != -ENOTSUP) {
		LOG_WRN("ctrl 0x%x=%d: %d", id, val, ret);
	}
	return ret;
}

static void requeue_all(void)
{
	struct video_buffer *vb;

	while (video_dequeue(video, &vb, K_NO_WAIT) == 0) {
		video_enqueue(video, vb);
	}
}

int cam_apply(const struct cfg *c)
{
	const struct cfg_resolution *r = &cfg_resolutions[c->resolution];
	struct video_format fmt = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width = r->width,
		.height = r->height,
	};
	struct video_buffer *vb;
	int ret;

	if (!device_is_ready(video)) {
		return -ENODEV;
	}

	k_mutex_lock(&cam_lock, K_FOREVER);

	/* Stopping flushes every buffer to the output queue; drain it so none is queued twice. */
	video_stream_stop(video, VIDEO_BUF_TYPE_OUTPUT);
	while (video_dequeue(video, &vb, K_NO_WAIT) == 0) {
	}

	ret = video_set_format(video, &fmt);
	if (ret < 0) {
		LOG_ERR("%s JPEG not supported (%d)", r->name, ret);
		goto out;
	}

	for (int i = 0; i < NBUF; i++) {
		if (bufs[i] != NULL && bufs[i]->size < fmt.size) {
			video_buffer_release(bufs[i]);
			bufs[i] = NULL;
		}
		if (bufs[i] == NULL) {
			bufs[i] = video_buffer_aligned_alloc(
				fmt.size, CONFIG_VIDEO_BUFFER_POOL_ALIGN, K_NO_WAIT);
			if (bufs[i] == NULL) {
				LOG_ERR("No memory for a %u B frame", fmt.size);
				ret = -ENOMEM;
				goto out;
			}
		}
		bufs[i]->type = VIDEO_BUF_TYPE_OUTPUT;
		video_enqueue(video, bufs[i]);
	}

	set_ctrl(VIDEO_CID_JPEG_COMPRESSION_QUALITY, c->quality);
	set_ctrl(VIDEO_CID_HFLIP, c->hflip);
	set_ctrl(VIDEO_CID_VFLIP, c->vflip);
	use_flash = c->flash;

	ret = video_stream_start(video, VIDEO_BUF_TYPE_OUTPUT);
	if (ret == 0) {
		LOG_INF("Streaming %s JPEG, %u B buffers", r->name, fmt.size);
	}
out:
	k_mutex_unlock(&cam_lock);
	return ret;
}

int cam_init(void)
{
	struct cfg c;

	if (!device_is_ready(video)) {
		LOG_ERR("%s not ready", video->name);
		return -ENODEV;
	}
	if (!led_is_ready_dt(&flash_led)) {
		return -ENODEV;
	}
	cfg_get(&c);
	return cam_apply(&c);
}

int cam_snapshot(const uint8_t **jpeg, size_t *len)
{
	int ret;

	if (!device_is_ready(video)) {
		return -ENODEV;
	}

	k_mutex_lock(&cam_lock, K_FOREVER);

	if (use_flash) {
		led_set_brightness_dt(&flash_led, FLASH_PERCENT);
		k_sleep(FLASH_SETTLE);
	}

	/* Frames already queued are stale: hand them back and wait for a new one. */
	requeue_all();
	ret = video_dequeue(video, &held, FRAME_TIMEOUT);

	if (use_flash) {
		led_off_dt(&flash_led);
	}

	if (ret < 0) {
		LOG_WRN("No frame (%d)", ret);
		k_mutex_unlock(&cam_lock);
		return ret;
	}

	*jpeg = held->buffer;
	*len = held->bytesused;
	return 0;
}

void cam_release(void)
{
	video_enqueue(video, held);
	held = NULL;
	k_mutex_unlock(&cam_lock);
}
