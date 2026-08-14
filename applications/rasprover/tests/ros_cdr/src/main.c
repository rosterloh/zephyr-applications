/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Golden-byte tests for the rasprover ROS 2 CDR encoders in app_ros_cdr.c.
 *
 * Why golden bytes: a wrong pad byte does not fail the build, it fails at the
 * ROS 2 subscriber, which silently misparses. The expected arrays below are
 * derived by hand from the message IDL plus the CDR alignment rule, NOT by
 * running the code under test, so they actually pin the wire layout.
 *
 * CDR rules used throughout:
 *   - 4-byte encapsulation header {0x00, 0x01, 0x00, 0x00} = CDR_LE, options 0.
 *   - Every primitive is aligned to its own size *relative to the byte after
 *     the encapsulation header* (the "origin"), so a f64 at relative offset 40
 *     sits at absolute offset 44.
 *   - A string is a uint32 byte count (including the NUL) followed by the bytes
 *     and the NUL. An empty string is therefore {01 00 00 00, 00} = 5 bytes.
 *   - A sequence is a uint32 element count followed by the elements.
 *   - Padding is zero-filled.
 */

#include <math.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "app_ros_cdr.h"

/*
 * sensor_msgs/BatteryState, encoded with stamp = {0x11223344, 0x55667788},
 * voltage = 12.5f, current = -1.5f.
 *
 * IEEE-754 single precision, little endian:
 *    12.5f = 1.5625 x 2^3     -> 0x41480000 -> 00 00 48 41
 *   -1.5f  = -1.5 x 2^0       -> 0xBFC00000 -> 00 00 C0 BF
 *   qNaN   = F32_QNAN_LE      -> 0x7FC00000 -> 00 00 C0 7F
 *
 * abs  rel  bytes           field
 * ---  ---  --------------  --------------------------------------------------
 *   0   -   00 01 00 00     encapsulation header; origin = 4
 *   4   0   44 33 22 11     header.stamp.sec
 *   8   4   88 77 66 55     header.stamp.nanosec
 *  12   8   01 00 00 00     header.frame_id length ("" + NUL = 1)
 *  16  12   00              header.frame_id bytes
 *  17  13   00 00 00        PAD 3: rel 13 -> 16 to 4-align the next float32
 *  20  16   00 00 48 41     voltage          = 12.5f
 *  24  20   00 00 C0 7F     temperature      = qNaN (unmeasured)
 *  28  24   00 00 C0 BF     current          = -1.5f
 *  32  28   00 00 C0 7F     charge           = qNaN
 *  36  32   00 00 C0 7F     capacity         = qNaN
 *  40  36   00 00 C0 7F     design_capacity  = qNaN
 *  44  40   00 00 C0 7F     percentage       = qNaN
 *  48  44   02              power_supply_status (DISCHARGING)
 *  49  45   02              power_supply_health (see BUG note below)
 *  50  46   00              power_supply_technology (UNKNOWN)
 *  51  47   01              present = true
 *  52  48   00 00 00 00     cell_voltage count = 0 (rel 48 is already 4-aligned)
 *  56  52   00 00 00 00     cell_temperature count = 0
 *  60  56   01 00 00 00     location length
 *  64  60   00              location bytes
 *  65  61   00 00 00        PAD 3: rel 61 -> 64 to 4-align the next length
 *  68  64   01 00 00 00     serial_number length
 *  72  68   00              serial_number bytes
 * total = 73 bytes
 */
/* clang-format off */
static const uint8_t battery_golden[] = {
	0x00, 0x01, 0x00, 0x00, /* encapsulation */
	0x44, 0x33, 0x22, 0x11, /* stamp.sec */
	0x88, 0x77, 0x66, 0x55, /* stamp.nanosec */
	0x01, 0x00, 0x00, 0x00, /* frame_id length */
	0x00,                   /* frame_id NUL */
	0x00, 0x00, 0x00,       /* pad to 4 */
	0x00, 0x00, 0x48, 0x41, /* voltage 12.5f */
	0x00, 0x00, 0xC0, 0x7F, /* temperature NaN */
	0x00, 0x00, 0xC0, 0xBF, /* current -1.5f */
	0x00, 0x00, 0xC0, 0x7F, /* charge NaN */
	0x00, 0x00, 0xC0, 0x7F, /* capacity NaN */
	0x00, 0x00, 0xC0, 0x7F, /* design_capacity NaN */
	0x00, 0x00, 0xC0, 0x7F, /* percentage NaN */
	0x02,                   /* power_supply_status */
	0x02,                   /* power_supply_health */
	0x00,                   /* power_supply_technology */
	0x01,                   /* present */
	0x00, 0x00, 0x00, 0x00, /* cell_voltage count */
	0x00, 0x00, 0x00, 0x00, /* cell_temperature count */
	0x01, 0x00, 0x00, 0x00, /* location length */
	0x00,                   /* location NUL */
	0x00, 0x00, 0x00,       /* pad to 4 */
	0x01, 0x00, 0x00, 0x00, /* serial_number length */
	0x00,                   /* serial_number NUL */
};
/* clang-format on */

/*
 * sensor_msgs/JointState, encoded with stamp = {1, 2} and a single joint
 * {"pan_joint", position = 1.0, velocity = -2.0}.
 *
 * IEEE-754 double precision, little endian:
 *    1.0 -> 0x3FF0000000000000 -> 00 00 00 00 00 00 F0 3F
 *   -2.0 -> 0xC000000000000000 -> 00 00 00 00 00 00 00 C0
 *
 * abs  rel  bytes                     field
 * ---  ---  ------------------------  ----------------------------------------
 *   0   -   00 01 00 00               encapsulation header; origin = 4
 *   4   0   01 00 00 00               header.stamp.sec = 1
 *   8   4   02 00 00 00               header.stamp.nanosec = 2
 *  12   8   01 00 00 00               header.frame_id length
 *  16  12   00                        header.frame_id bytes
 *  17  13   00 00 00                  PAD 3: rel 13 -> 16
 *  20  16   01 00 00 00               name count = 1
 *  24  20   0A 00 00 00               name[0] length ("pan_joint" + NUL = 10)
 *  28  24   70 61 6E 5F 6A 6F 69 6E   "pan_join"
 *  36  32   74 00                     "t" NUL
 *  38  34   00 00                     PAD 2: rel 34 -> 36
 *  40  36   01 00 00 00               position count = 1
 *  44  40   ...F0 3F                  position[0] = 1.0 (rel 40 % 8 == 0: NO pad)
 *  52  48   01 00 00 00               velocity count = 1
 *  56  52   00 00 00 00               PAD 4: rel 52 -> 56 to 8-align the float64
 *  60  56   ...00 C0                  velocity[0] = -2.0
 *  68  64   00 00 00 00               effort count = 0
 * total = 72 bytes
 */
/* clang-format off */
static const uint8_t joint_golden[] = {
	0x00, 0x01, 0x00, 0x00,                                     /* encapsulation */
	0x01, 0x00, 0x00, 0x00,                                     /* stamp.sec */
	0x02, 0x00, 0x00, 0x00,                                     /* stamp.nanosec */
	0x01, 0x00, 0x00, 0x00,                                     /* frame_id length */
	0x00,                                                       /* frame_id NUL */
	0x00, 0x00, 0x00,                                           /* pad to 4 */
	0x01, 0x00, 0x00, 0x00,                                     /* name count */
	0x0A, 0x00, 0x00, 0x00,                                     /* name[0] length */
	'p',  'a',  'n',  '_',  'j', 'o', 'i', 'n', 't', 0x00,      /* name[0] bytes */
	0x00, 0x00,                                                 /* pad to 4 */
	0x01, 0x00, 0x00, 0x00,                                     /* position count */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F,             /* position[0] 1.0 */
	0x01, 0x00, 0x00, 0x00,                                     /* velocity count */
	0x00, 0x00, 0x00, 0x00,                                     /* pad to 8 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0,             /* velocity[0] -2.0 */
	0x00, 0x00, 0x00, 0x00,                                     /* effort count */
};
/* clang-format on */

static float f32_at(const uint8_t *buf, size_t off)
{
	float v;

	memcpy(&v, buf + off, sizeof(v));
	return v;
}

ZTEST_SUITE(ros_cdr, NULL, NULL, NULL, NULL, NULL);

ZTEST(ros_cdr, test_battery_state_golden_bytes)
{
	uint8_t buf[128];
	const struct app_ros_time stamp = {.sec = 0x11223344u, .nanosec = 0x55667788u};
	size_t len = app_ros_encode_battery_state(buf, sizeof(buf), stamp, 12.5f, -1.5f);

	zassert_equal(len, sizeof(battery_golden), "encoded %zu bytes, expected %zu", len,
		      sizeof(battery_golden));
	zassert_mem_equal(buf, battery_golden, sizeof(battery_golden),
			  "BatteryState wire layout changed");
}

/*
 * The two transitions that hand-rolled CDR gets wrong. Asserted separately from
 * the golden array so a failure names the boundary rather than "bytes differ".
 */
ZTEST(ros_cdr, test_battery_state_alignment_boundaries)
{
	uint8_t buf[128];
	const struct app_ros_time stamp = {.sec = 0, .nanosec = 0};

	(void)app_ros_encode_battery_state(buf, sizeof(buf), stamp, 12.5f, -1.5f);

	/* float32 after the empty frame_id string: 3 pad bytes at abs 17..19. */
	zassert_equal(buf[17], 0, "pad byte 0 before voltage is not zero");
	zassert_equal(buf[18], 0, "pad byte 1 before voltage is not zero");
	zassert_equal(buf[19], 0, "pad byte 2 before voltage is not zero");
	zassert_equal(f32_at(buf, 20), 12.5f, "voltage is not at absolute offset 20");

	/*
	 * uint32 after the four uint8 flags: rel 48 is already 4-aligned, so
	 * there must be NO padding. An over-eager align here would shift every
	 * following field by 4.
	 */
	zassert_equal(buf[51], 1, "present flag is not at absolute offset 51");
	zassert_equal(buf[52], 0, "cell_voltage count does not start at absolute offset 52");

	/* uint32 length after the empty location string: 3 pad bytes at 65..67. */
	zassert_equal(buf[65], 0, "pad byte 0 before serial_number is not zero");
	zassert_equal(buf[66], 0, "pad byte 1 before serial_number is not zero");
	zassert_equal(buf[67], 0, "pad byte 2 before serial_number is not zero");
	zassert_equal(buf[68], 1, "serial_number length does not start at absolute offset 68");
}

/* The five "if unmeasured, NaN" fields must decode as NaN, not as 0.0. */
ZTEST(ros_cdr, test_battery_state_nan_sentinels)
{
	uint8_t buf[128];
	const struct app_ros_time stamp = {.sec = 0, .nanosec = 0};
	const size_t nan_offsets[] = {24, 32, 36, 40, 44}; /* temperature, charge,
							    * capacity, design_capacity,
							    * percentage
							    */

	(void)app_ros_encode_battery_state(buf, sizeof(buf), stamp, 12.5f, -1.5f);

	ARRAY_FOR_EACH(nan_offsets, i) {
		zassert_true(isnan(f32_at(buf, nan_offsets[i])), "field at offset %zu is not NaN",
			     nan_offsets[i]);
	}

	/* The measured fields must NOT be NaN. */
	zassert_equal(f32_at(buf, 20), 12.5f, "voltage");
	zassert_equal(f32_at(buf, 28), -1.5f, "current");
}

/*
 * BUG (behaviour asserted as-is, deliberately not fixed here):
 * app_ros_cdr.c writes 2 for power_supply_health and comments it
 * "POWER_SUPPLY_HEALTH_GOOD". In sensor_msgs/BatteryState the constants are
 * POWER_SUPPLY_HEALTH_UNKNOWN = 0, _GOOD = 1, _OVERHEAT = 2. The wire therefore
 * says the pack is OVERHEATING. The status byte (2 = DISCHARGING) and the
 * technology byte (0 = UNKNOWN) are correct.
 */
ZTEST(ros_cdr, test_battery_state_power_supply_enums)
{
	uint8_t buf[128];
	const struct app_ros_time stamp = {.sec = 0, .nanosec = 0};

	(void)app_ros_encode_battery_state(buf, sizeof(buf), stamp, 12.5f, -1.5f);

	zassert_equal(buf[48], 2, "power_supply_status should be DISCHARGING (2)");
	zassert_equal(buf[49], 2,
		      "power_supply_health currently encodes OVERHEAT (2); "
		      "GOOD is 1 -- see the BUG note above this test");
	zassert_equal(buf[50], 0, "power_supply_technology should be UNKNOWN (0)");
	zassert_equal(buf[51], 1, "present should be true");
}

/* cdr_write_encapsulation() must emit CDR_LE (0x0001) with a zero options word. */
ZTEST(ros_cdr, test_encapsulation_header)
{
	uint8_t battery[128];
	uint8_t joint[128];
	const struct app_ros_time stamp = {.sec = 0, .nanosec = 0};
	const struct app_ros_joint_sample sample = {
		.name = "pan_joint", .position = 1.0, .velocity = -2.0};
	static const uint8_t expected[] = {0x00, 0x01, 0x00, 0x00};

	(void)app_ros_encode_battery_state(battery, sizeof(battery), stamp, 12.5f, -1.5f);
	(void)app_ros_encode_joint_state(joint, sizeof(joint), stamp, &sample, 1);

	zassert_mem_equal(battery, expected, sizeof(expected), "BatteryState encapsulation");
	zassert_mem_equal(joint, expected, sizeof(expected), "JointState encapsulation");
}

/*
 * Trust boundary: the caller supplies the buffer. Contract per cdr.c is that
 * an overflowing write sets a sticky error flag, cdr_writer_finish() then
 * returns 0, and nothing is written past the declared capacity.
 */
ZTEST(ros_cdr, test_battery_state_buffer_bounds)
{
	uint8_t buf[128];
	const struct app_ros_time stamp = {.sec = 0x11223344u, .nanosec = 0x55667788u};

	/* Exact fit succeeds. */
	zassert_equal(
		app_ros_encode_battery_state(buf, sizeof(battery_golden), stamp, 12.5f, -1.5f),
		sizeof(battery_golden), "exact-size buffer should encode fully");

	/* One byte short fails, and the guard byte past the limit is untouched. */
	memset(buf, 0xAA, sizeof(buf));
	zassert_equal(
		app_ros_encode_battery_state(buf, sizeof(battery_golden) - 1, stamp, 12.5f, -1.5f),
		0, "undersized buffer must report 0 bytes");
	zassert_equal(buf[sizeof(battery_golden) - 1], 0xAA, "wrote past the caller's buffer");

	/* A buffer too small even for the encapsulation header must not scribble. */
	memset(buf, 0xAA, sizeof(buf));
	zassert_equal(app_ros_encode_battery_state(buf, 2, stamp, 12.5f, -1.5f), 0,
		      "2-byte buffer must report 0 bytes");
	zassert_equal(buf[2], 0xAA, "wrote past a 2-byte buffer");
}

ZTEST(ros_cdr, test_joint_state_golden_bytes)
{
	uint8_t buf[128];
	const struct app_ros_time stamp = {.sec = 1, .nanosec = 2};
	const struct app_ros_joint_sample sample = {
		.name = "pan_joint", .position = 1.0, .velocity = -2.0};
	size_t len = app_ros_encode_joint_state(buf, sizeof(buf), stamp, &sample, 1);

	zassert_equal(len, sizeof(joint_golden), "encoded %zu bytes, expected %zu", len,
		      sizeof(joint_golden));
	zassert_mem_equal(buf, joint_golden, sizeof(joint_golden),
			  "JointState wire layout changed");

	/*
	 * The float64 boundary specifically: position[0] needs no padding
	 * (rel 40), velocity[0] needs 4 bytes (rel 52 -> 56). Getting this
	 * wrong is the classic hand-rolled-CDR failure.
	 */
	zassert_equal(buf[40], 0x01, "position count moved off absolute offset 40");
	zassert_mem_equal(&buf[56], "\x00\x00\x00\x00", 4, "missing 4-byte pad before velocity[0]");
}

ZTEST(ros_cdr, test_joint_state_buffer_bounds)
{
	uint8_t buf[128];
	const struct app_ros_time stamp = {.sec = 1, .nanosec = 2};
	const struct app_ros_joint_sample sample = {
		.name = "pan_joint", .position = 1.0, .velocity = -2.0};

	memset(buf, 0xAA, sizeof(buf));
	zassert_equal(app_ros_encode_joint_state(buf, sizeof(joint_golden) - 1, stamp, &sample, 1),
		      0, "undersized buffer must report 0 bytes");
	zassert_equal(buf[sizeof(joint_golden) - 1], 0xAA, "wrote past the caller's buffer");

	/* A NULL sample array with a non-zero count is rejected before any write. */
	zassert_equal(app_ros_encode_joint_state(buf, sizeof(buf), stamp, NULL, 1), 0,
		      "NULL joints with count > 0 must be rejected");
}

/*
 * Round trip: the decoder walks the same alignment rules as the encoder, so a
 * mismatch between the two shows up here even if both golden arrays agree.
 */
ZTEST(ros_cdr, test_joint_command_round_trip)
{
	uint8_t buf[192];
	const struct app_ros_time stamp = {.sec = 7, .nanosec = 8};
	const struct app_ros_joint_sample samples[] = {
		{.name = "tilt_joint", .position = 0.25, .velocity = 0.0},
		{.name = "pan_joint", .position = -0.5, .velocity = 0.0},
	};
	struct app_ros_joint_command cmd = {0};
	size_t len =
		app_ros_encode_joint_state(buf, sizeof(buf), stamp, samples, ARRAY_SIZE(samples));

	zassert_true(len > 0, "encode failed");
	zassert_true(app_ros_decode_joint_command(buf, len, &cmd), "decode failed");
	zassert_true(cmd.has_pan, "pan_joint not found");
	zassert_true(cmd.has_tilt, "tilt_joint not found");
	zassert_equal(cmd.pan_position, -0.5, "pan position mismatch");
	zassert_equal(cmd.tilt_position, 0.25, "tilt position mismatch");
}

ZTEST(ros_cdr, test_joint_command_rejects_bad_input)
{
	uint8_t buf[192];
	const struct app_ros_time stamp = {.sec = 0, .nanosec = 0};
	const struct app_ros_joint_sample samples[] = {
		{.name = "tilt_joint", .position = 0.25, .velocity = 0.0},
		{.name = "pan_joint", .position = -0.5, .velocity = 0.0},
	};
	struct app_ros_joint_command cmd = {0};
	size_t len =
		app_ros_encode_joint_state(buf, sizeof(buf), stamp, samples, ARRAY_SIZE(samples));

	zassert_true(len > 0, "encode failed");

	/* Big-endian encapsulation (CDR_BE) must be refused, not misparsed. */
	buf[1] = 0x00;
	zassert_false(app_ros_decode_joint_command(buf, len, &cmd),
		      "non-CDR_LE encapsulation must be rejected");
	buf[1] = 0x01;

	/*
	 * Truncation. Absolute offsets for this two-joint message, derived the
	 * same way as the golden arrays above:
	 *   20  name count = 2
	 *   24  name[0] length, 28..38 "tilt_joint\0", 39 pad
	 *   40  name[1] length, 44..53 "pan_joint\0", 54..55 pad
	 *   56  position count = 2
	 *   60  position[0]   68  position[1]
	 *   76  velocity count, 80..83 pad, 84 velocity[0], 92 velocity[1]
	 *  100  effort count            -> total 104
	 */
	zassert_equal(len, 104, "two-joint layout changed; update the offsets below");

	/* Cut right after the name count: name[0] cannot be read. */
	zassert_false(app_ros_decode_joint_command(buf, 24, &cmd),
		      "payload truncated inside the name array must be rejected");

	/* Cut inside position[1], which spans 68..75. */
	zassert_false(app_ros_decode_joint_command(buf, 72, &cmd),
		      "payload truncated inside the position array must be rejected");

	/*
	 * Not a defect, but pinned so it stays deliberate: the decoder stops
	 * after the position array, so a payload missing the trailing velocity
	 * and effort fields is still accepted.
	 */
	zassert_true(app_ros_decode_joint_command(buf, 76, &cmd),
		     "decoder must not require the velocity/effort fields");

	/* Only one of the two joints present -> rejected. */
	len = app_ros_encode_joint_state(buf, sizeof(buf), stamp, &samples[1], 1);
	zassert_true(len > 0, "encode failed");
	zassert_false(app_ros_decode_joint_command(buf, len, &cmd),
		      "a command without tilt_joint must be rejected");

	zassert_false(app_ros_decode_joint_command(NULL, 0, &cmd), "NULL buffer must be rejected");
}
