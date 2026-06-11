#ifndef APP_ROS_CDR_H
#define APP_ROS_CDR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct app_ros_time {
	uint32_t sec;
	uint32_t nanosec;
};

struct app_ros_joint_sample {
	const char *name;
	double position;
	double velocity;
};

struct app_ros_joint_command {
	bool has_pan;
	bool has_tilt;
	double pan_position;
	double tilt_position;
};

size_t app_ros_encode_battery_state(uint8_t *buf, size_t buf_size, struct app_ros_time stamp,
				    float voltage, float current);

size_t app_ros_encode_joint_state(uint8_t *buf, size_t buf_size, struct app_ros_time stamp,
				  const struct app_ros_joint_sample *joints, size_t joint_count);

bool app_ros_decode_joint_command(const uint8_t *buf, size_t len,
				  struct app_ros_joint_command *out);

#endif /* APP_ROS_CDR_H */
