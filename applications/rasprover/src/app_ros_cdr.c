#include "app_ros_cdr.h"

#include <stdbool.h>
#include <string.h>

#define CDR_HEADER_SIZE 4
#define F32_QNAN_LE     0x7FC00000u

struct cdr_writer {
	uint8_t *buf;
	size_t len;
	size_t cap;
	bool ok;
};

static size_t cdr_rel(const struct cdr_writer *w)
{
	return (w->len >= CDR_HEADER_SIZE) ? (w->len - CDR_HEADER_SIZE) : 0;
}

static void cdr_reserve(struct cdr_writer *w, size_t len)
{
	if (w->len + len > w->cap) {
		w->ok = false;
	}
}

static void cdr_write_zeroes(struct cdr_writer *w, size_t len)
{
	cdr_reserve(w, len);
	if (!w->ok) {
		return;
	}
	memset(w->buf + w->len, 0, len);
	w->len += len;
}

static void cdr_align(struct cdr_writer *w, size_t alignment)
{
	size_t rem = cdr_rel(w) % alignment;

	if (rem != 0) {
		cdr_write_zeroes(w, alignment - rem);
	}
}

static void cdr_write_bytes(struct cdr_writer *w, const void *src, size_t len)
{
	cdr_reserve(w, len);
	if (!w->ok) {
		return;
	}
	memcpy(w->buf + w->len, src, len);
	w->len += len;
}

static void cdr_write_u32(struct cdr_writer *w, uint32_t v)
{
	uint8_t bytes[4] = {
		(uint8_t)v,
		(uint8_t)(v >> 8),
		(uint8_t)(v >> 16),
		(uint8_t)(v >> 24),
	};

	cdr_align(w, 4);
	cdr_write_bytes(w, bytes, sizeof(bytes));
}

static void cdr_write_f32(struct cdr_writer *w, float v)
{
	cdr_align(w, 4);
	cdr_write_bytes(w, &v, sizeof(v));
}

static void cdr_write_f64(struct cdr_writer *w, double v)
{
	cdr_align(w, 8);
	cdr_write_bytes(w, &v, sizeof(v));
}

static void cdr_write_string(struct cdr_writer *w, const char *s)
{
	size_t len = strlen(s) + 1;

	cdr_write_u32(w, (uint32_t)len);
	cdr_write_bytes(w, s, len);
}

static void cdr_write_header(struct cdr_writer *w, struct app_ros_time stamp)
{
	static const uint8_t cdr_le_header[CDR_HEADER_SIZE] = {0x00, 0x01, 0x00, 0x00};

	cdr_write_bytes(w, cdr_le_header, sizeof(cdr_le_header));
	cdr_write_u32(w, stamp.sec);
	cdr_write_u32(w, stamp.nanosec);
	cdr_write_string(w, "");
}

static void cdr_write_f32_nan(struct cdr_writer *w)
{
	cdr_write_u32(w, F32_QNAN_LE);
}

size_t app_ros_encode_battery_state(uint8_t *buf, size_t buf_size, struct app_ros_time stamp,
				    float voltage, float current)
{
	struct cdr_writer w = {
		.buf = buf,
		.cap = buf_size,
		.ok = true,
	};

	cdr_write_header(&w, stamp);
	cdr_write_f32(&w, voltage);
	cdr_write_f32_nan(&w); /* temperature */
	cdr_write_f32(&w, current);
	cdr_write_f32_nan(&w); /* charge */
	cdr_write_f32_nan(&w); /* capacity */
	cdr_write_f32_nan(&w); /* design_capacity */
	cdr_write_f32_nan(&w); /* percentage */

	cdr_write_bytes(&w, &(const uint8_t){2}, 1); /* POWER_SUPPLY_STATUS_DISCHARGING */
	cdr_write_bytes(&w, &(const uint8_t){2}, 1); /* POWER_SUPPLY_HEALTH_GOOD */
	cdr_write_bytes(&w, &(const uint8_t){0}, 1); /* POWER_SUPPLY_TECHNOLOGY_UNKNOWN */
	cdr_write_bytes(&w, &(const uint8_t){1}, 1); /* present */

	cdr_write_u32(&w, 0); /* cell_voltage */
	cdr_write_u32(&w, 0); /* cell_temperature */
	cdr_write_string(&w, "");
	cdr_write_string(&w, "");

	return w.ok ? w.len : 0;
}

size_t app_ros_encode_joint_state(uint8_t *buf, size_t buf_size, struct app_ros_time stamp,
				  const struct app_ros_joint_sample *joints, size_t joint_count)
{
	struct cdr_writer w = {
		.buf = buf,
		.cap = buf_size,
		.ok = true,
	};

	if (joints == NULL && joint_count != 0) {
		return 0;
	}

	cdr_write_header(&w, stamp);

	cdr_write_u32(&w, (uint32_t)joint_count);
	for (size_t i = 0; i < joint_count; i++) {
		if (joints[i].name == NULL) {
			return 0;
		}
		cdr_write_string(&w, joints[i].name);
	}

	cdr_write_u32(&w, (uint32_t)joint_count);
	for (size_t i = 0; i < joint_count; i++) {
		cdr_write_f64(&w, joints[i].position);
	}

	cdr_write_u32(&w, (uint32_t)joint_count);
	for (size_t i = 0; i < joint_count; i++) {
		cdr_write_f64(&w, joints[i].velocity);
	}

	cdr_write_u32(&w, 0); /* effort */

	return w.ok ? w.len : 0;
}
