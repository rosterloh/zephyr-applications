#include "app_ros_cdr.h"

#include <string.h>
#include <zephyr/cdr/cdr.h>

#define JOINT_CMD_MAX_NAMES 8

static void encode_header(struct cdr_writer *w, struct app_ros_time stamp)
{
	cdr_write_encapsulation(w);
	cdr_write_u32(w, stamp.sec);
	cdr_write_u32(w, stamp.nanosec);
	cdr_write_string(w, ""); /* frame_id */
}

size_t app_ros_encode_joint_state(uint8_t *buf, size_t buf_size, struct app_ros_time stamp,
				  const struct app_ros_joint_sample *joints, size_t joint_count)
{
	struct cdr_writer w;

	if (joints == NULL && joint_count != 0) {
		return 0;
	}

	cdr_writer_init(&w, buf, buf_size);
	encode_header(&w, stamp);

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

	return cdr_writer_finish(&w);
}

bool app_ros_decode_joint_command(const uint8_t *buf, size_t len, struct app_ros_joint_command *out)
{
	bool is_pan[JOINT_CMD_MAX_NAMES] = {0};
	bool is_tilt[JOINT_CMD_MAX_NAMES] = {0};

	if (buf == NULL || out == NULL) {
		return false;
	}

	struct cdr_reader r;

	cdr_reader_init(&r, buf, len);
	if (!cdr_read_encapsulation(&r)) {
		return false;
	}

	struct app_ros_joint_command cmd = {0};

	(void)cdr_read_u32(&r); /* stamp.sec */
	(void)cdr_read_u32(&r); /* stamp.nanosec */
	cdr_skip_string(&r);    /* frame_id */

	uint32_t name_count = cdr_read_u32(&r);

	if (!cdr_reader_ok(&r) || name_count > JOINT_CMD_MAX_NAMES) {
		return false;
	}

	for (uint32_t i = 0; i < name_count; i++) {
		const char *name;
		uint32_t name_len;

		if (!cdr_read_string_ref(&r, &name, &name_len)) {
			return false;
		}
		is_pan[i] = (name_len == sizeof("pan_joint")) &&
			    (memcmp(name, "pan_joint", name_len) == 0);
		is_tilt[i] = (name_len == sizeof("tilt_joint")) &&
			     (memcmp(name, "tilt_joint", name_len) == 0);
	}

	uint32_t position_count = cdr_read_u32(&r);

	if (!cdr_reader_ok(&r) || position_count != name_count) {
		return false;
	}

	for (uint32_t i = 0; i < position_count; i++) {
		double position = cdr_read_f64(&r);

		if (!cdr_reader_ok(&r)) {
			return false;
		}
		if (is_pan[i]) {
			cmd.has_pan = true;
			cmd.pan_position = position;
		}
		if (is_tilt[i]) {
			cmd.has_tilt = true;
			cmd.tilt_position = position;
		}
	}

	if (!cmd.has_pan || !cmd.has_tilt) {
		return false;
	}

	*out = cmd;
	return true;
}
