#include <zephyr/sys/util.h>

#include "teleop.h"

static double scale(int16_t v, double max)
{
	return CLAMP(v, -TELEOP_AXIS_MAX, TELEOP_AXIS_MAX) * max / TELEOP_AXIS_MAX;
}

bool teleop_step(struct teleop_state *s, const struct teleop_input *in,
		 const struct teleop_limits *lim, struct teleop_twist *out)
{
	if (s->blocked) {
		s->blocked = in->enable;
		return false;
	}

	if (in->enable) {
		s->active = true;
		out->linear_x = scale(in->y, lim->max_linear);
		out->linear_y = scale(in->x, lim->max_linear);
		out->angular_z = scale(in->rz, lim->max_angular);
		return true;
	}

	if (s->active) {
		s->active = false;
		*out = (struct teleop_twist){0};
		return true;
	}

	return false;
}

void teleop_reset(struct teleop_state *s)
{
	s->active = false;
	s->blocked = true;
}

void teleop_cal_symmetric(int16_t rest, int16_t seen_min, int16_t seen_max, int16_t *in_min,
			  int16_t *in_max)
{
	int16_t span = MIN(rest - seen_min, seen_max - rest);

	*in_min = rest - span;
	*in_max = rest + span;
}
