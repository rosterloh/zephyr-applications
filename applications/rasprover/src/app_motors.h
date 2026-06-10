#ifndef __APP_MOTORS_H__
#define __APP_MOTORS_H__

#include <stdbool.h>

bool app_motors_init(void);

/* Differential-drive a geometry_msgs/Twist: linear.x (m/s), angular.z (rad/s). */
void app_motors_cmd_vel(float linear_x, float angular_z);

#endif /* __APP_MOTORS_H__ */
