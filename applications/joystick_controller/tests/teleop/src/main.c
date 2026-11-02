#include <zephyr/ztest.h>

#include "teleop.h"

static const struct teleop_limits lim = {.max_linear = 0.5, .max_angular = 1.0};

static bool step_mod(struct teleop_state *s, bool enable, bool mod, int16_t x, int16_t y,
		     int16_t rz, struct teleop_twist *out)
{
	const struct teleop_input in = {.enable = enable, .mod = mod, .x = x, .y = y, .rz = rz};

	return teleop_step(s, &in, &lim, out);
}

static bool step(struct teleop_state *s, bool enable, int16_t x, int16_t y, int16_t rz,
		 struct teleop_twist *out)
{
	return step_mod(s, enable, false, x, y, rz, out);
}

static bool grasp(struct teleop_state *s, bool enable, bool pressed, bool *out)
{
	const struct teleop_input in = {.enable = enable, .grasp = pressed};
	struct teleop_twist twist;

	(void)teleop_step(s, &in, &lim, &twist);
	return teleop_grasp_step(s, &in, out);
}

/* Enabled with the stick centred: armed, nothing sent. */
static void arm(struct teleop_state *s, bool mod)
{
	struct teleop_twist out;

	zassert_false(step_mod(s, true, mod, 0, 0, 0, &out));
}

static void assert_zero(const struct teleop_twist *t)
{
	zassert_within(t->linear_x, 0.0, 1e-9);
	zassert_within(t->linear_y, 0.0, 1e-9);
	zassert_within(t->linear_z, 0.0, 1e-9);
	zassert_within(t->angular_x, 0.0, 1e-9);
	zassert_within(t->angular_y, 0.0, 1e-9);
	zassert_within(t->angular_z, 0.0, 1e-9);
}

ZTEST(teleop, test_idle_is_silent)
{
	struct teleop_state s = {0};
	struct teleop_twist out;

	zassert_false(step(&s, false, 500, 500, 500, &out));
}

ZTEST(teleop, test_enable_with_stick_deflected_waits_for_centre)
{
	struct teleop_state s = {0};
	struct teleop_twist out;

	zassert_false(step(&s, true, 0, 1000, 0, &out));
	zassert_false(step(&s, true, 0, 1000, 0, &out));
	zassert_false(step(&s, true, 0, 0, 0, &out));
	zassert_true(step(&s, true, 0, 1000, 0, &out));
}

ZTEST(teleop, test_held_scales_holonomic)
{
	struct teleop_state s = {0};
	struct teleop_twist out;

	arm(&s, false);
	zassert_true(step(&s, true, 1000, -500, 250, &out));
	zassert_within(out.linear_x, -0.25, 1e-9);
	zassert_within(out.linear_y, 0.5, 1e-9);
	zassert_within(out.angular_z, 0.25, 1e-9);
	zassert_within(out.linear_z, 0.0, 1e-9);
	zassert_within(out.angular_x, 0.0, 1e-9);
	zassert_within(out.angular_y, 0.0, 1e-9);
}

ZTEST(teleop, test_mod_drives_lift_pitch_roll)
{
	struct teleop_state s = {0};
	struct teleop_twist out;

	arm(&s, true);
	zassert_true(step_mod(&s, true, true, 1000, -500, 250, &out));
	zassert_within(out.linear_z, -0.25, 1e-9);
	zassert_within(out.angular_y, 1.0, 1e-9);
	zassert_within(out.angular_x, 0.25, 1e-9);
	zassert_within(out.linear_x, 0.0, 1e-9);
	zassert_within(out.linear_y, 0.0, 1e-9);
	zassert_within(out.angular_z, 0.0, 1e-9);
}

ZTEST(teleop, test_mod_change_stops_and_waits_for_centre)
{
	struct teleop_state s = {0};
	struct teleop_twist out;

	arm(&s, false);
	zassert_true(step(&s, true, 0, 1000, 0, &out));
	/* Mod pressed mid-move: one stop, then nothing until the stick centres. */
	zassert_true(step_mod(&s, true, true, 0, 1000, 0, &out));
	assert_zero(&out);
	zassert_false(step_mod(&s, true, true, 0, 1000, 0, &out));
	zassert_false(step_mod(&s, true, true, 0, 0, 0, &out));
	zassert_true(step_mod(&s, true, true, 0, 1000, 0, &out));
	zassert_within(out.linear_z, 0.5, 1e-9);
}

ZTEST(teleop, test_centring_publishes_zero_once)
{
	struct teleop_state s = {0};
	struct teleop_twist out;

	arm(&s, false);
	zassert_true(step(&s, true, 0, 1000, 0, &out));
	zassert_true(step(&s, true, 0, 0, 0, &out));
	assert_zero(&out);
	zassert_false(step(&s, true, 0, 0, 0, &out));
	/* Still armed: moving again needs no re-centre. */
	zassert_true(step(&s, true, 0, 1000, 0, &out));
}

ZTEST(teleop, test_release_publishes_zero_once)
{
	struct teleop_state s = {0};
	struct teleop_twist out;

	arm(&s, false);
	zassert_true(step(&s, true, 0, 1000, 0, &out));
	zassert_true(step(&s, false, 0, 1000, 0, &out));
	assert_zero(&out);
	zassert_false(step(&s, false, 0, 1000, 0, &out));
	/* Re-enabled with the stick still deflected: not armed. */
	zassert_false(step(&s, true, 0, 1000, 0, &out));
}

ZTEST(teleop, test_clamps_out_of_range)
{
	struct teleop_state s = {0};
	struct teleop_twist out;

	arm(&s, false);
	zassert_true(step(&s, true, -1500, 1500, 0, &out));
	zassert_within(out.linear_x, 0.5, 1e-9);
	zassert_within(out.linear_y, -0.5, 1e-9);
}

ZTEST(teleop, test_reset_blocks_until_release)
{
	struct teleop_state s = {0};
	struct teleop_twist out;

	arm(&s, false);
	zassert_true(step(&s, true, 0, 1000, 0, &out));
	teleop_reset(&s);
	zassert_false(step(&s, true, 0, 0, 0, &out));
	zassert_false(step(&s, true, 0, 1000, 0, &out));
	zassert_false(step(&s, false, 0, 1000, 0, &out));
	arm(&s, false);
	zassert_true(step(&s, true, 0, 1000, 0, &out));
}

ZTEST(teleop, test_boot_with_enable_latched_on_sends_nothing)
{
	struct teleop_state s;
	struct teleop_twist out;
	bool g;

	teleop_reset(&s);
	zassert_false(step(&s, true, 0, 0, 0, &out));
	zassert_false(step(&s, true, 0, 1000, 0, &out));
	zassert_false(grasp(&s, true, true, &g));
}

ZTEST(teleop, test_grasp_published_on_change_while_enabled)
{
	struct teleop_state s = {0};
	bool g = false;

	zassert_false(grasp(&s, false, true, &g));
	zassert_true(grasp(&s, true, true, &g));
	zassert_true(g);
	zassert_false(grasp(&s, true, true, &g));
	zassert_true(grasp(&s, true, false, &g));
	zassert_false(g);
	zassert_false(grasp(&s, false, true, &g));
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
