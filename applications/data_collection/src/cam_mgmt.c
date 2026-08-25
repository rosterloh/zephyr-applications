/*
 * Copyright (c) 2026 Richard Osterloh
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Custom MCUmgr (SMP) management group for the camera, so a host can drive
 * capture over the same SMP/UDP transport this app already uses for OTA
 * instead of only the boot-time capture and the interactive `video` shell.
 * Commands: INFO (read) / CAPTURE (write) / READ (read).
 * Wire contract: see "SMP camera group" in README.md.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/video.h>
#include <zephyr/video/formats.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zcbor_common.h>
#include <zcbor_encode.h>
#include <zcbor_decode.h>
#include <mgmt/mcumgr/util/zcbor_bulk.h>

#include "cam_mgmt.h"

LOG_MODULE_REGISTER(cam_mgmt, LOG_LEVEL_INF);

/* Vendor/custom group ids start at MGMT_GROUP_ID_PERUSER (64), and the
 * Zephyr-specific groups count *down* from there, so anything comfortably above
 * 64 is safe. 0x1000 keeps that distance and matches the convention used by
 * other out-of-tree SMP groups. */
#define CAM_MGMT_GROUP_ID      0x1000
#define CAM_MGMT_GROUP_VERSION 1

#define CAM_MGMT_CMD_INFO    0
#define CAM_MGMT_CMD_CAPTURE 1
#define CAM_MGMT_CMD_READ    2

/* Largest chunk a single READ returns. The binding limit is the buffer the
 * response is encoded into, CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE (2048 when
 * MCUMGR_TRANSPORT_UDP is set, 384 otherwise); the datagram then has to fit
 * CONFIG_MCUMGR_TRANSPORT_UDP_MTU (1500). 1024 plus the 8-byte SMP header and
 * the CBOR map around the byte string lands near 1100 B, inside both, and is a
 * round number for clients to page with. Raise it against NETBUF_SIZE *and*
 * the MTU, not just one of them. */
#define CAM_MGMT_READ_MAX 1024

#define CAPTURE_NBUFS 2

/* The app's PSRAM budget, expressed as a resolution ceiling rather than a
 * format. A sensor advertising a stepwise range -- the IMX219 goes to
 * 3280x2464 -- would otherwise have us ask for an 8 MB frame when the pool
 * cannot hold two of them.
 *
 * ponytail: assumes the ceiling lands on the sensor's width/height step (it
 * does for the IMX219's step of 4). If a sensor with a coarser step appears,
 * round the clamped value down to cap->width_min + n * cap->width_step.
 */
#define CAPTURE_MAX_WIDTH  1640
#define CAPTURE_MAX_HEIGHT 1232

/* The negotiated format, filled in by the first capture. INFO and CAPTURE
 * report it so a client sees what the camera actually agreed to rather than a
 * compile-time guess -- the two differ as soon as the shield changes.
 */
static struct video_format frame_fmt;

/* Buffer ownership: the captured frame stays checked out of the video buffer
 * pool until the next CAPTURE. A frame is ~2.5 MB of PSRAM, so READ serves it
 * in place rather than copying it into a second buffer. The pool only holds
 * CONFIG_VIDEO_BUFFER_POOL_NUM_MAX buffers and the CSI driver needs
 * CAPTURE_NBUFS of them to stream, which is why the retained frame is released
 * at the top of the next capture rather than kept alongside it. `frame_lock`
 * stops a READ on the SMP thread from touching a buffer that the boot-time
 * capture on the main thread is releasing. */
static struct video_buffer *frame_vbuf;
static uint32_t frame_seq;
K_MUTEX_DEFINE(frame_lock);

/* Choose the advertised format with the highest bit depth that fits the
 * ceiling. Depth first because this is a data-collection app: a sensor offering
 * both RAW8 and RAW10 should give us RAW10. Sizes are clamped rather than
 * rejected, so a stepwise sensor lands on the largest frame we can hold while a
 * sensor with one fixed small size (the ToF module's 240x180) is taken as-is.
 */
static int cam_pick_format(const struct device *cam, struct video_format *fmt)
{
	struct video_caps caps = {.type = VIDEO_BUF_TYPE_OUTPUT};
	unsigned int best_bpp = 0;
	int ret;

	ret = video_get_caps(cam, &caps);
	if (ret < 0) {
		LOG_ERR("Failed to get caps (%d)", ret);
		return ret;
	}

	for (int i = 0; caps.format_caps[i].pixelformat != 0; i++) {
		const struct video_format_cap *cap = &caps.format_caps[i];
		unsigned int bpp = video_bits_per_pixel(cap->pixelformat);

		if (bpp <= best_bpp) {
			continue;
		}

		fmt->pixelformat = cap->pixelformat;
		fmt->width = MIN(cap->width_max, CAPTURE_MAX_WIDTH);
		fmt->height = MIN(cap->height_max, CAPTURE_MAX_HEIGHT);
		best_bpp = bpp;
	}

	if (best_bpp == 0) {
		LOG_ERR("Camera advertises no usable format");
		return -ENOTSUP;
	}

	fmt->type = VIDEO_BUF_TYPE_OUTPUT;

	return 0;
}

int cam_mgmt_capture(const struct device *cam)
{
	struct video_format fmt;
	struct video_buffer *vbuf = NULL;
	struct video_buffer *drained;
	int ret;

	k_mutex_lock(&frame_lock, K_FOREVER);

	if (frame_vbuf != NULL) {
		video_buffer_release(frame_vbuf);
		frame_vbuf = NULL;
	}

	ret = cam_pick_format(cam, &fmt);
	if (ret < 0) {
		goto out;
	}

	ret = video_set_format(cam, &fmt);
	if (ret < 0) {
		LOG_ERR("Failed to set format (%d)", ret);
		goto out;
	}

	ret = video_get_format(cam, &fmt);
	if (ret < 0) {
		goto out;
	}
	LOG_INF("Capturing %ux%u, pitch %u, %u bytes/frame", fmt.width, fmt.height, fmt.pitch,
		fmt.pitch * fmt.height);
	frame_fmt = fmt;

	for (int i = 0; i < CAPTURE_NBUFS; i++) {
		/* Round the allocation up to a whole D-cache line. The CSI driver
		 * invalidates the buffer around each DMA, and an invalidate that ends
		 * mid-line discards the dirty half of that line belonging to whatever
		 * the heap placed next -- in practice the following buffer's chunk
		 * header, which then faults sys_heap_free(). POOL_ALIGN only aligns the
		 * start; video_buffer_aligned_alloc() passes the size through as-is.
		 */
		vbuf = video_buffer_aligned_alloc(
			ROUND_UP(fmt.pitch * fmt.height, CONFIG_DCACHE_LINE_SIZE),
			CONFIG_VIDEO_BUFFER_POOL_ALIGN, K_NO_WAIT);
		if (vbuf == NULL) {
			LOG_ERR("Failed to allocate video buffer %d", i);
			ret = -ENOMEM;
			goto drain;
		}
		vbuf->type = VIDEO_BUF_TYPE_OUTPUT;
		ret = video_enqueue(cam, vbuf);
		if (ret < 0) {
			LOG_ERR("Failed to enqueue buffer %d (%d)", i, ret);
			video_buffer_release(vbuf);
			goto drain;
		}
	}

	ret = video_stream_start(cam, VIDEO_BUF_TYPE_OUTPUT);
	if (ret < 0) {
		LOG_ERR("Failed to start stream (%d)", ret);
		goto drain;
	}

	ret = video_dequeue(cam, &vbuf, K_SECONDS(2));
	if (ret < 0) {
		LOG_ERR("Failed to dequeue frame (%d)", ret);
		goto drain;
	}

	frame_vbuf = vbuf;
	frame_seq++;
	LOG_INF("Frame %u captured: %u bytes, first pixels %02x %02x %02x %02x", frame_seq,
		vbuf->bytesused, vbuf->buffer[0], vbuf->buffer[1], vbuf->buffer[2],
		vbuf->buffer[3]);

drain:
	/* video_stream_stop() cancels whatever is still queued into the done queue
	 * (and does so even if the stream never started), so draining it here
	 * covers both the success path and every failure above. Skip the retained
	 * frame by identity: no driver should hand back a buffer we already
	 * dequeued, but releasing it here would serve freed PSRAM to any SMP
	 * client that asks, so don't depend on that. */
	video_stream_stop(cam, VIDEO_BUF_TYPE_OUTPUT);
	while (video_dequeue(cam, &drained, K_NO_WAIT) == 0) {
		if (drained != frame_vbuf) {
			video_buffer_release(drained);
		}
	}
out:
	k_mutex_unlock(&frame_lock);
	return ret;
}

/* INFO (read): {} -> {group, cam, fmt, w, h, ready}. The format triple is what
 * the camera negotiated on the most recent CAPTURE, and is zero before the
 * first one. The authoritative frame length is still CAPTURE's `size`, and
 * clients must size their buffer from that. */
static int cam_h_info(struct smp_streamer *ctxt)
{
	const struct device *cam = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));
	zcbor_state_t *zse = ctxt->writer->zs;
	bool ok;

	/* frame_fmt is written by cam_mgmt_capture() under frame_lock, and the
	 * boot-time capture runs on the main thread while this handler runs on the
	 * SMP thread. Read the triple under the lock or a concurrent capture can
	 * hand back a mixed answer -- the old pixelformat with the new width.
	 */
	k_mutex_lock(&frame_lock, K_FOREVER);
	ok = zcbor_tstr_put_lit(zse, "group") && zcbor_uint32_put(zse, CAM_MGMT_GROUP_VERSION) &&
	     zcbor_tstr_put_lit(zse, "cam") && zcbor_tstr_put_term(zse, cam->name, 32) &&
	     zcbor_tstr_put_lit(zse, "fmt") && zcbor_uint32_put(zse, frame_fmt.pixelformat) &&
	     zcbor_tstr_put_lit(zse, "w") && zcbor_uint32_put(zse, frame_fmt.width) &&
	     zcbor_tstr_put_lit(zse, "h") && zcbor_uint32_put(zse, frame_fmt.height) &&
	     zcbor_tstr_put_lit(zse, "ready") && zcbor_bool_put(zse, device_is_ready(cam));
	k_mutex_unlock(&frame_lock);

	return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

/* CAPTURE (write): {} -> {seq, size, w, h, fmt}. A write because it discards the
 * previously retained frame and drives the sensor. */
static int cam_h_capture(struct smp_streamer *ctxt)
{
	const struct device *cam = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));
	zcbor_state_t *zse = ctxt->writer->zs;
	bool ok;

	if (cam_mgmt_capture(cam) < 0) {
		return MGMT_ERR_EUNKNOWN;
	}

	k_mutex_lock(&frame_lock, K_FOREVER);
	/* cam_mgmt_capture() drops the lock before returning, so a capture on
	 * another thread can have released our frame and then failed by the time we
	 * get here. Report it gone rather than dereferencing NULL. */
	if (frame_vbuf == NULL) {
		k_mutex_unlock(&frame_lock);
		return MGMT_ERR_ENOENT;
	}

	ok = zcbor_tstr_put_lit(zse, "seq") && zcbor_uint32_put(zse, frame_seq) &&
	     zcbor_tstr_put_lit(zse, "size") && zcbor_uint32_put(zse, frame_vbuf->bytesused) &&
	     zcbor_tstr_put_lit(zse, "w") && zcbor_uint32_put(zse, frame_fmt.width) &&
	     zcbor_tstr_put_lit(zse, "h") && zcbor_uint32_put(zse, frame_fmt.height) &&
	     zcbor_tstr_put_lit(zse, "fmt") && zcbor_uint32_put(zse, frame_fmt.pixelformat);
	k_mutex_unlock(&frame_lock);

	return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

/* READ (read): {seq, off, len} -> {seq, off, data, eof}. Cursor-paged pull of
 * the retained frame; the client repeats with off += len(data) until eof. */
static int cam_h_read(struct smp_streamer *ctxt)
{
	zcbor_state_t *zsd = ctxt->reader->zs;
	zcbor_state_t *zse = ctxt->writer->zs;
	uint32_t seq = 0, off = 0, want = CAM_MGMT_READ_MAX;
	struct zcbor_string data;
	size_t decoded;
	bool ok, eof;
	struct zcbor_map_decode_key_val dk[] = {
		ZCBOR_MAP_DECODE_KEY_DECODER("seq", zcbor_uint32_decode, &seq),
		ZCBOR_MAP_DECODE_KEY_DECODER("off", zcbor_uint32_decode, &off),
		ZCBOR_MAP_DECODE_KEY_DECODER("len", zcbor_uint32_decode, &want),
	};

	if (zcbor_map_decode_bulk(zsd, dk, ARRAY_SIZE(dk), &decoded) != 0) {
		return MGMT_ERR_EINVAL;
	}

	if (want == 0 || want > CAM_MGMT_READ_MAX) {
		want = CAM_MGMT_READ_MAX;
	}

	k_mutex_lock(&frame_lock, K_FOREVER);

	/* Refuse rather than silently serve the wrong frame: the buffer holds
	 * exactly one, and a newer CAPTURE replaces it mid-pull. seq 0 never
	 * matches, so "nothing captured yet" answers the same way. */
	if (frame_vbuf == NULL || seq != frame_seq) {
		k_mutex_unlock(&frame_lock);
		return MGMT_ERR_ENOENT;
	}

	off = MIN(off, frame_vbuf->bytesused);
	want = MIN(want, frame_vbuf->bytesused - off);
	eof = (off + want) >= frame_vbuf->bytesused;
	data.value = frame_vbuf->buffer + off;
	data.len = want;

	ok = zcbor_tstr_put_lit(zse, "seq") && zcbor_uint32_put(zse, seq) &&
	     zcbor_tstr_put_lit(zse, "off") && zcbor_uint32_put(zse, off) &&
	     zcbor_tstr_put_lit(zse, "data") && zcbor_bstr_encode(zse, &data) &&
	     zcbor_tstr_put_lit(zse, "eof") && zcbor_bool_put(zse, eof);

	k_mutex_unlock(&frame_lock);

	return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

static const struct mgmt_handler cam_mgmt_handlers[] = {
	[CAM_MGMT_CMD_INFO] = {cam_h_info, NULL},
	[CAM_MGMT_CMD_CAPTURE] = {NULL, cam_h_capture},
	[CAM_MGMT_CMD_READ] = {cam_h_read, NULL},
};

static struct mgmt_group cam_mgmt_group = {
	.mg_handlers = cam_mgmt_handlers,
	.mg_handlers_count = ARRAY_SIZE(cam_mgmt_handlers),
	.mg_group_id = CAM_MGMT_GROUP_ID,
};

static void cam_mgmt_register(void)
{
	mgmt_register_group(&cam_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(cam_mgmt, cam_mgmt_register);
