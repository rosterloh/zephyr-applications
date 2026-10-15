#include <zephyr/ztest.h>

#include "teleop.h"

static const struct teleop_limits lim = {.max_linear = 0.5, .max_angular = 1.0};

static bool step(struct teleop_state *s, bool enable, int16_t x, int16_t y, int16_t rz,
		 struct teleop_twist *out)
{
	const struct teleop_input in = {.enable = enable, .x = x, .y = y, .rz = rz};

	return teleop_step(s, &in, &lim, out);
}

ZTEST(teleop, test_idle_is_silent)
{
	struct teleop_state s = {0};
	struct teleop_twist out;

	zassert_false(step(&s, false, 500, 500, 500, &out));
}

ZTEST(teleop, test_held_scales_holonomic)
{
	struct teleop_state s = {0};
	struct teleop_twist out;

	zassert_true(step(&s, true, 1000, -500, 250, &out));
	zassert_within(out.linear_x, -0.25, 1e-9);
	zassert_within(out.linear_y, 0.5, 1e-9);
	zassert_within(out.angular_z, 0.25, 1e-9);
}

ZTEST(teleop, test_release_publishes_zero_once)
{
	struct teleop_state s = {0};
	struct teleop_twist out;

	zassert_true(step(&s, true, 0, 1000, 0, &out));
	zassert_true(step(&s, false, 0, 1000, 0, &out));
	zassert_within(out.linear_x, 0.0, 1e-9);
	zassert_within(out.linear_y, 0.0, 1e-9);
	zassert_within(out.angular_z, 0.0, 1e-9);
	zassert_false(step(&s, false, 0, 1000, 0, &out));
}

ZTEST(teleop, test_clamps_out_of_range)
{
	struct teleop_state s = {0};
	struct teleop_twist out;

	zassert_true(step(&s, true, -1500, 1500, 0, &out));
	zassert_within(out.linear_x, 0.5, 1e-9);
	zassert_within(out.linear_y, -0.5, 1e-9);
}

ZTEST(teleop, test_reset_blocks_until_release)
{
	struct teleop_state s = {0};
	struct teleop_twist out;

	zassert_true(step(&s, true, 0, 1000, 0, &out));
	teleop_reset(&s);
	zassert_false(step(&s, true, 0, 1000, 0, &out));
	zassert_false(step(&s, true, 0, 1000, 0, &out));
	zassert_false(step(&s, false, 0, 1000, 0, &out));
	zassert_true(step(&s, true, 0, 1000, 0, &out));
}

ZTEST(teleop, test_cal_symmetric_about_rest)
{
	int16_t min, max;

	zassert_true(teleop_cal_symmetric(1800, 900, 3000, 100, &min, &max));
	zassert_equal(min, 900);
	zassert_equal(max, 2700);

	zassert_true(teleop_cal_symmetric(2048, 100, 4000, 100, &min, &max));
	zassert_equal(min, 100);
	zassert_equal(max, 3996);
}

ZTEST(teleop, test_cal_rejects_one_sided_sweep)
{
	int16_t min = 0, max = 0;

	/* Moved only one way: total sweep is large, one side is not. */
	zassert_false(teleop_cal_symmetric(2000, 1950, 3500, 100, &min, &max));
	zassert_false(teleop_cal_symmetric(2000, 500, 2050, 100, &min, &max));
	zassert_equal(min, 0);
	zassert_equal(max, 0);
}

ZTEST_SUITE(teleop, NULL, NULL, NULL, NULL, NULL);
