#ifndef APP_ROS_CDR_H
#define APP_ROS_CDR_H

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

size_t app_ros_encode_battery_state(uint8_t *buf, size_t buf_size, struct app_ros_time stamp,
				    float voltage, float current);

size_t app_ros_encode_joint_state(uint8_t *buf, size_t buf_size, struct app_ros_time stamp,
				  const struct app_ros_joint_sample *joints, size_t joint_count);

#endif /* APP_ROS_CDR_H */
