#include <zephyr/sys/util.h>

#include "teleop.h"

static double scale(int16_t v, double max)
{
	return CLAMP(v, -TELEOP_AXIS_MAX, TELEOP_AXIS_MAX) * max / TELEOP_AXIS_MAX;
}

/* analog-axis reports exactly 0 inside its deadzone. */
static bool centred(const struct teleop_input *in)
{
	return in->x == 0 && in->y == 0 && in->rz == 0;
}

static bool stop(struct teleop_state *s, struct teleop_twist *out)
{
	if (!s->active) {
		return false;
	}
	s->active = false;
	*out = (struct teleop_twist){0};
	return true;
}

bool teleop_step(struct teleop_state *s, const struct teleop_input *in,
		 const struct teleop_limits *lim, struct teleop_twist *out)
{
	if (s->blocked) {
		s->blocked = in->enable;
		return false;
	}

	if (!in->enable || in->mod != s->mod) {
		s->armed = false;
		s->mod = in->mod;
		if (stop(s, out)) {
			return true;
		}
		if (!in->enable) {
			return false;
		}
		/* Nothing owed: a centred stick can arm on this same tick. */
	}

	if (!s->armed) {
		s->armed = centred(in);
		return false;
	}

	if (centred(in)) {
		return stop(s, out);
	}

	s->active = true;
	*out = (struct teleop_twist){0};
	if (in->mod) {
		out->linear_z = scale(in->y, lim->max_linear);
		out->angular_y = scale(in->x, lim->max_angular);
		out->angular_x = scale(in->rz, lim->max_angular);
	} else {
		out->linear_x = scale(in->y, lim->max_linear);
		out->linear_y = scale(in->x, lim->max_linear);
		out->angular_z = scale(in->rz, lim->max_angular);
	}
	return true;
}

bool teleop_grasp_step(struct teleop_state *s, const struct teleop_input *in, bool *grasp)
{
	if (s->blocked || !in->enable || in->grasp == s->grasp) {
		return false;
	}
	s->grasp = in->grasp;
	*grasp = in->grasp;
	return true;
}

void teleop_reset(struct teleop_state *s)
{
	*s = (struct teleop_state){.blocked = true};
}

bool teleop_cal_symmetric(int16_t rest, int16_t seen_min, int16_t seen_max, int16_t min_span,
			  int16_t *in_min, int16_t *in_max)
{
	int16_t span = MIN(rest - seen_min, seen_max - rest);

	if (span < min_span) {
		return false;
	}

	*in_min = rest - span;
	*in_max = rest + span;
	return true;
}
