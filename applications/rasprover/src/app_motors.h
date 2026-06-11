#ifndef __APP_MOTORS_H__
#define __APP_MOTORS_H__

#include <stdbool.h>

#define APP_MOTORS_JOINT_COUNT 2

struct app_motors_joint_state {
	const char *name;
	double position_rad;
	double velocity_rad_s;
};

bool app_motors_init(void);

/* Differential-drive a geometry_msgs/Twist: linear.x (m/s), angular.z (rad/s). */
void app_motors_cmd_vel(float linear_x, float angular_z);

bool app_motors_read_joint_state(struct app_motors_joint_state joints[APP_MOTORS_JOINT_COUNT]);

#endif /* __APP_MOTORS_H__ */
