#ifndef TELEOP_H_
#define TELEOP_H_

#include <stdbool.h>
#include <stdint.h>

/* analog-axis out-max; out-min is -TELEOP_AXIS_MAX */
#define TELEOP_AXIS_MAX 1000

struct teleop_limits {
	double max_linear;  /* m/s */
	double max_angular; /* rad/s */
};

struct teleop_input {
	bool enable;
	int16_t x;
	int16_t y;
	int16_t rz;
};

struct teleop_twist {
	double linear_x;
	double linear_y;
	double angular_z;
};

/* Zero-initialised is the boot state. */
struct teleop_state {
	bool active;
	bool blocked;
};

/* Returns true when *out must be published this tick. */
bool teleop_step(struct teleop_state *s, const struct teleop_input *in,
		 const struct teleop_limits *lim, struct teleop_twist *out);

/* Link lost: go inactive and ignore enable until it is seen released. */
void teleop_reset(struct teleop_state *s);

/* Largest span symmetric about rest that fits inside [seen_min, seen_max].
 * Returns false, leaving the outputs untouched, if either side of rest moved
 * less than min_span (stick only swept one way, or was not at rest at start).
 */
bool teleop_cal_symmetric(int16_t rest, int16_t seen_min, int16_t seen_max, int16_t min_span,
			  int16_t *in_min, int16_t *in_max);

#endif /* TELEOP_H_ */
