/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * native_sim camera: every frame is sim/frame.jpg, one per FRAME_PERIOD while
 * streaming. Frames arrive from a work item, as from DMA on hardware, so
 * cam.c's drain-and-requeue loop sees an empty queue between frames.
 */

#define DT_DRV_COMPAT vnd_jpeg_camera

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>

#define FRAME_PERIOD K_MSEC(50)

static const uint8_t frame[] = {
#include "sim_frame.jpg.inc"
};

struct sim_cam_data {
	struct video_format fmt;
	struct k_fifo in;
	struct k_fifo out;
	struct k_work_delayable work;
	bool streaming;
};

static void sim_cam_work(struct k_work *work)
{
	struct sim_cam_data *data =
		CONTAINER_OF(k_work_delayable_from_work(work), struct sim_cam_data, work);
	struct video_buffer *vb = k_fifo_get(&data->in, K_NO_WAIT);

	if (vb == NULL || !data->streaming) {
		if (vb != NULL) {
			k_fifo_put(&data->in, vb);
		}
		return;
	}
	memcpy(vb->buffer, frame, sizeof(frame));
	vb->bytesused = sizeof(frame);
	vb->timestamp = k_uptime_get_32();
	k_fifo_put(&data->out, vb);
	k_work_reschedule(&data->work, FRAME_PERIOD);
}

static int sim_cam_set_format(const struct device *dev, struct video_format *fmt)
{
	struct sim_cam_data *data = dev->data;

	if (fmt->pixelformat != VIDEO_PIX_FMT_JPEG) {
		return -ENOTSUP;
	}
	fmt->pitch = 0;
	fmt->size = sizeof(frame);
	data->fmt = *fmt;
	return 0;
}

static int sim_cam_get_format(const struct device *dev, struct video_format *fmt)
{
	*fmt = ((struct sim_cam_data *)dev->data)->fmt;
	return 0;
}

static int sim_cam_set_stream(const struct device *dev, bool enable, enum video_buf_type type)
{
	struct sim_cam_data *data = dev->data;

	data->streaming = enable;
	if (enable) {
		k_work_reschedule(&data->work, FRAME_PERIOD);
	}
	return 0;
}

static int sim_cam_enqueue(const struct device *dev, struct video_buffer *vb)
{
	struct sim_cam_data *data = dev->data;

	if (vb->size < sizeof(frame)) {
		return -ENOMEM;
	}
	k_fifo_put(&data->in, vb);
	if (data->streaming && !k_work_delayable_is_pending(&data->work)) {
		k_work_reschedule(&data->work, FRAME_PERIOD);
	}
	return 0;
}

static int sim_cam_dequeue(const struct device *dev, struct video_buffer **vb, k_timeout_t timeout)
{
	*vb = k_fifo_get(&((struct sim_cam_data *)dev->data)->out, timeout);
	return *vb == NULL ? -EAGAIN : 0;
}

static int sim_cam_flush(const struct device *dev, bool cancel)
{
	struct sim_cam_data *data = dev->data;
	struct video_buffer *vb;

	if (cancel) {
		k_work_cancel_delayable(&data->work);
		while ((vb = k_fifo_get(&data->in, K_NO_WAIT)) != NULL) {
			k_fifo_put(&data->out, vb);
		}
	}
	return 0;
}

static DEVICE_API(video, sim_cam_api) = {
	.set_format = sim_cam_set_format,
	.get_format = sim_cam_get_format,
	.set_stream = sim_cam_set_stream,
	.enqueue = sim_cam_enqueue,
	.dequeue = sim_cam_dequeue,
	.flush = sim_cam_flush,
};

static int sim_cam_init(const struct device *dev)
{
	struct sim_cam_data *data = dev->data;

	k_fifo_init(&data->in);
	k_fifo_init(&data->out);
	k_work_init_delayable(&data->work, sim_cam_work);
	data->fmt = (struct video_format){
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width = 320,
		.height = 240,
		.size = sizeof(frame),
	};
	return 0;
}

static struct sim_cam_data sim_cam_data;

DEVICE_DT_INST_DEFINE(0, sim_cam_init, NULL, &sim_cam_data, NULL, POST_KERNEL,
		      CONFIG_VIDEO_INIT_PRIORITY, &sim_cam_api);
