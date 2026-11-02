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
	bool mod; /* stick drives lift/pitch/roll instead of x/y/yaw */
	bool grasp;
	int16_t x;
	int16_t y;
	int16_t rz;
};

struct teleop_twist {
	double linear_x;
	double linear_y;
	double linear_z;
	double angular_x;
	double angular_y;
	double angular_z;
};

/* Start from teleop_reset(): blocked until enable is seen off, so a latched
 * enable switch left on at power-up or across a link drop sends nothing.
 */
struct teleop_state {
	bool active;  /* last published twist was motion, so a stop is owed */
	bool blocked; /* enable must be seen off before anything is sent */
	bool armed;   /* stick seen centred since enable, or since mod last changed */
	bool mod;     /* the mod level that arming applies to */
	bool grasp;   /* last grasp level published */
};

/* Returns true when *out must be published this tick: motion while the stick
 * is deflected, then one all-zero twist when it centres, enable goes off or
 * mod changes. Silent otherwise, so lower-priority command sources aren't
 * blocked. After enable or a mod change the stick must centre before it
 * drives anything.
 */
bool teleop_step(struct teleop_state *s, const struct teleop_input *in,
		 const struct teleop_limits *lim, struct teleop_twist *out);

/* Call after teleop_step() each tick. Returns true when *grasp must be
 * published: its level changed while enabled and not blocked.
 */
bool teleop_grasp_step(struct teleop_state *s, const struct teleop_input *in, bool *grasp);

/* Boot and link lost: go inactive and ignore enable until it is seen off. */
void teleop_reset(struct teleop_state *s);

/* Largest span symmetric about rest that fits inside [seen_min, seen_max].
 * Returns false, leaving the outputs untouched, if either side of rest moved
 * less than min_span (stick only swept one way, or was not at rest at start).
 */
bool teleop_cal_symmetric(int16_t rest, int16_t seen_min, int16_t seen_max, int16_t min_span,
			  int16_t *in_min, int16_t *in_max);

#endif /* TELEOP_H_ */
