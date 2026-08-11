/*
 * Copyright (c) 2026 Richard Osterloh
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/drivers/video.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* IMX219 2x2-binned full-FoV RAW10 frame. */
#define CAPTURE_WIDTH  1640
#define CAPTURE_HEIGHT 1232
#define CAPTURE_FORMAT VIDEO_PIX_FMT_SBGGR10P
#define CAPTURE_NBUFS  2

static struct net_mgmt_event_callback ipv4_cb;

static void on_ipv4_addr_add(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			     struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
		return;
	}

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		struct net_if_addr *if_addr = &iface->config.ip.ipv4->unicast[i].ipv4;
		char buf[NET_IPV4_ADDR_LEN];

		if (!if_addr->is_used || if_addr->addr_type != NET_ADDR_DHCP) {
			continue;
		}

		LOG_INF("IPv4 address: %s",
			net_addr_ntop(AF_INET, &if_addr->address.in_addr, buf, sizeof(buf)));
	}
}

static int capture_one_frame(const struct device *cam)
{
	struct video_format fmt = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = CAPTURE_FORMAT,
		.width = CAPTURE_WIDTH,
		.height = CAPTURE_HEIGHT,
	};
	struct video_buffer *vbuf;
	int ret;

	ret = video_set_format(cam, &fmt);
	if (ret < 0) {
		LOG_ERR("Failed to set format (%d)", ret);
		return ret;
	}

	ret = video_get_format(cam, &fmt);
	if (ret < 0) {
		return ret;
	}
	LOG_INF("Capturing %ux%u, pitch %u, %u bytes/frame", fmt.width, fmt.height, fmt.pitch,
		fmt.pitch * fmt.height);

	for (int i = 0; i < CAPTURE_NBUFS; i++) {
		vbuf = video_buffer_aligned_alloc(fmt.pitch * fmt.height,
						  CONFIG_VIDEO_BUFFER_POOL_ALIGN, K_NO_WAIT);
		if (vbuf == NULL) {
			LOG_ERR("Failed to allocate video buffer %d", i);
			return -ENOMEM;
		}
		vbuf->type = VIDEO_BUF_TYPE_OUTPUT;
		ret = video_enqueue(cam, vbuf);
		if (ret < 0) {
			LOG_ERR("Failed to enqueue buffer %d (%d)", i, ret);
			return ret;
		}
	}

	ret = video_stream_start(cam, VIDEO_BUF_TYPE_OUTPUT);
	if (ret < 0) {
		LOG_ERR("Failed to start stream (%d)", ret);
		return ret;
	}

	ret = video_dequeue(cam, &vbuf, K_SECONDS(2));
	if (ret < 0) {
		LOG_ERR("Failed to dequeue frame (%d)", ret);
		video_stream_stop(cam, VIDEO_BUF_TYPE_OUTPUT);
		return ret;
	}

	LOG_INF("Frame captured: %u bytes, first pixels %02x %02x %02x %02x", vbuf->bytesused,
		vbuf->buffer[0], vbuf->buffer[1], vbuf->buffer[2], vbuf->buffer[3]);

	video_stream_stop(cam, VIDEO_BUF_TYPE_OUTPUT);
	return 0;
}

int main(void)
{
	const struct device *cam = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));

	LOG_INF("data_collection starting (SMP/UDP management ready)");

	net_mgmt_init_event_callback(&ipv4_cb, on_ipv4_addr_add, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);

	if (!device_is_ready(cam)) {
		LOG_ERR("Camera %s not ready", cam->name);
		return 0;
	}

	if (capture_one_frame(cam) == 0) {
		LOG_INF("Camera capture OK; use the `video` shell for further captures");
	}

	return 0;
}
