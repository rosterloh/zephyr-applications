#ifndef __APP_GIMBAL_H__
#define __APP_GIMBAL_H__

#include <stdbool.h>

#define APP_GIMBAL_JOINT_COUNT 2

struct app_gimbal_joint_state {
	const char *name;
	double position_rad;
	double velocity_rad_s;
};

bool app_gimbal_init(void);
void app_gimbal_set_positions(float pan_rad, float tilt_rad);
bool app_gimbal_read_joint_state(struct app_gimbal_joint_state joints[APP_GIMBAL_JOINT_COUNT]);

#endif /* __APP_GIMBAL_H__ */
