/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Golden-byte tests for the rasprover ROS 2 messages, now encoded by Pico-ROS's
 * picoserdes from the type list in src/rasprover_types.h.
 *
 * Why golden bytes: a wrong pad byte does not fail the build, it fails at the
 * ROS 2 subscriber, which silently misparses. The expected arrays below are
 * derived by hand from the message IDL plus the CDR alignment rule, NOT by
 * running the code under test, so they actually pin the wire layout.
 *
 * They are also the same arrays the previous hand-rolled encoder
 * (app_ros_cdr.c) was tested against, unchanged. That is the point of keeping
 * them: they were validated against a live ROS 2 stack through
 * zenoh-bridge-ros2dds, so byte-for-byte equality is direct evidence that
 * moving to picoserdes did not change what goes on the wire.
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

#include <picoserdes.h>

/*
 * sensor_msgs/BatteryState, encoded with stamp = {0x11223344, 0x55667788},
 * voltage = 12.5f, current = -1.5f.
 *
 * IEEE-754 single precision, little endian:
 *    12.5f = 1.5625 x 2^3     -> 0x41480000 -> 00 00 48 41
 *   -1.5f  = -1.5 x 2^0       -> 0xBFC00000 -> 00 00 C0 BF
 *   qNaN                      -> 0x7FC00000 -> 00 00 C0 7F
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
 *  48  44   02              power_supply_status = DISCHARGING (2)
 *  49  45   01              power_supply_health = GOOD (1)
 *  50  46   00              power_supply_technology = UNKNOWN (0)
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
	0x02,                   /* power_supply_status = DISCHARGING */
	0x01,                   /* power_supply_health = GOOD */
	0x00,                   /* power_supply_technology = UNKNOWN */
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

/*
 * ps_serialize() stores the encapsulation header through a uint32_t*, so every
 * buffer handed to it must be 4-aligned. An unaligned store faults on Xtensa,
 * which is the target this firmware actually runs on.
 */
static uint8_t buf[256] __aligned(4);

static float f32_at(const uint8_t *b, size_t off)
{
	float v;

	memcpy(&v, b + off, sizeof(v));
	return v;
}

static size_t encode_battery(uint32_t sec, uint32_t nanosec, float voltage, float current)
{
	ros_BatteryState msg = {
		.header =
			{
				.stamp = {.sec = (int32_t)sec, .nanosec = nanosec},
				.frame_id = "",
			},
		.voltage = voltage,
		.temperature = NAN,
		.current = current,
		.charge = NAN,
		.capacity = NAN,
		.design_capacity = NAN,
		.percentage = NAN,
		.power_supply_status = 2,
		.power_supply_health = 1,
		.power_supply_technology = 0,
		.present = true,
		.cell_voltage = {.data = NULL, .n_elements = 0},
		.cell_temperature = {.data = NULL, .n_elements = 0},
		.location = "",
		.serial_number = "",
	};

	memset(buf, 0, sizeof(buf));
	return ps_serialize(buf, &msg, sizeof(buf));
}

static size_t encode_joints(uint32_t sec, uint32_t nanosec, rstring *names, double *positions,
			    double *velocities, uint32_t count)
{
	ros_JointState msg = {
		.header =
			{
				.stamp = {.sec = (int32_t)sec, .nanosec = nanosec},
				.frame_id = "",
			},
		.name = {.data = names, .n_elements = count},
		.position = {.data = positions, .n_elements = count},
		.velocity = {.data = velocities, .n_elements = count},
		.effort = {.data = NULL, .n_elements = 0},
	};

	memset(buf, 0, sizeof(buf));
	return ps_serialize(buf, &msg, sizeof(buf));
}

ZTEST_SUITE(picoserdes, NULL, NULL, NULL, NULL, NULL);

ZTEST(picoserdes, test_battery_state_golden_bytes)
{
	size_t len = encode_battery(0x11223344u, 0x55667788u, 12.5f, -1.5f);

	zassert_equal(len, sizeof(battery_golden), "encoded %zu bytes, expected %zu", len,
		      sizeof(battery_golden));
	zassert_mem_equal(buf, battery_golden, sizeof(battery_golden),
			  "BatteryState wire layout changed");
}

/*
 * The two transitions that CDR encoders get wrong. Asserted separately from
 * the golden array so a failure names the boundary rather than "bytes differ".
 */
ZTEST(picoserdes, test_battery_state_alignment_boundaries)
{
	(void)encode_battery(0, 0, 12.5f, -1.5f);

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
ZTEST(picoserdes, test_battery_state_nan_sentinels)
{
	const size_t nan_offsets[] = {24, 32, 36, 40, 44}; /* temperature, charge,
							    * capacity, design_capacity,
							    * percentage
							    */

	(void)encode_battery(0, 0, 12.5f, -1.5f);

	ARRAY_FOR_EACH(nan_offsets, i) {
		zassert_true(isnan(f32_at(buf, nan_offsets[i])), "field at offset %zu is not NaN",
			     nan_offsets[i]);
	}

	/* The measured fields must NOT be NaN. */
	zassert_equal(f32_at(buf, 20), 12.5f, "voltage");
	zassert_equal(f32_at(buf, 28), -1.5f, "current");
}

/*
 * The three enum bytes, pinned to their sensor_msgs/BatteryState values. These
 * are bare integers on the wire, so a wrong one is only visible to the
 * subscriber -- POWER_SUPPLY_HEALTH_GOOD is 1 and 2 is OVERHEAT, one apart.
 */
ZTEST(picoserdes, test_battery_state_power_supply_enums)
{
	(void)encode_battery(0, 0, 12.5f, -1.5f);

	zassert_equal(buf[48], 2, "power_supply_status should be DISCHARGING (2)");
	zassert_equal(buf[49], 1, "power_supply_health should be GOOD (1), not OVERHEAT (2)");
	zassert_equal(buf[50], 0, "power_supply_technology should be UNKNOWN (0)");
	zassert_equal(buf[51], 1, "present should be true");
}

/* ps_serialize() must emit CDR_LE (0x0001) with a zero options word. */
ZTEST(picoserdes, test_encapsulation_header)
{
	static const uint8_t expected[] = {0x00, 0x01, 0x00, 0x00};
	rstring names[] = {"pan_joint"};
	double positions[] = {1.0};
	double velocities[] = {-2.0};

	(void)encode_battery(0, 0, 12.5f, -1.5f);
	zassert_mem_equal(buf, expected, sizeof(expected), "BatteryState encapsulation");

	(void)encode_joints(0, 0, names, positions, velocities, 1);
	zassert_mem_equal(buf, expected, sizeof(expected), "JointState encapsulation");
}

ZTEST(picoserdes, test_joint_state_golden_bytes)
{
	rstring names[] = {"pan_joint"};
	double positions[] = {1.0};
	double velocities[] = {-2.0};
	size_t len = encode_joints(1, 2, names, positions, velocities, 1);

	zassert_equal(len, sizeof(joint_golden), "encoded %zu bytes, expected %zu", len,
		      sizeof(joint_golden));
	zassert_mem_equal(buf, joint_golden, sizeof(joint_golden),
			  "JointState wire layout changed");

	/*
	 * The float64 boundary specifically: position[0] needs no padding
	 * (rel 40), velocity[0] needs 4 bytes (rel 52 -> 56).
	 */
	zassert_equal(buf[40], 0x01, "position count moved off absolute offset 40");
	zassert_mem_equal(&buf[56], "\x00\x00\x00\x00", 4, "missing 4-byte pad before velocity[0]");
}

/*
 * The exact size the firmware's JointState buffer is sized against. If a joint
 * name changes length, or a joint is added, this fails and
 * JOINT_STATE_BUF_SIZE in app_picoros.c has to be re-derived -- picoserdes
 * silently truncates rather than reporting an overflow (see
 * test_serialize_overflow_is_silent below).
 */
ZTEST(picoserdes, test_joint_state_four_joint_size)
{
	rstring names[] = {"left_wheel_joint", "right_wheel_joint", "pan_joint", "tilt_joint"};
	double positions[] = {0.0, 0.0, 0.0, 0.0};
	double velocities[] = {0.0, 0.0, 0.0, 0.0};
	size_t len = encode_joints(1, 2, names, positions, velocities, ARRAY_SIZE(names));

	zassert_equal(len, 184, "four-joint JointState is %zu bytes, expected 184", len);
	zassert_true(len <= 320, "exceeds app_picoros.c's JOINT_STATE_BUF_SIZE");
}

/*
 * Round trip through the same type description the firmware uses, with the
 * sequence-capacity contract the gimbal command handler relies on:
 * n_elements is the caller's capacity going in, n_deserialized is the count
 * that came back.
 */
ZTEST(picoserdes, test_joint_state_round_trip)
{
	rstring tx_names[] = {"tilt_joint", "pan_joint"};
	double tx_positions[] = {0.25, -0.5};
	double tx_velocities[] = {0.0, 0.0};
	size_t len = encode_joints(7, 8, tx_names, tx_positions, tx_velocities, 2);

	rstring rx_names[8];
	double rx_positions[8];
	double rx_velocities[8];
	double rx_efforts[8];
	ros_JointState rx = {
		.name = {.data = rx_names, .n_elements = ARRAY_SIZE(rx_names)},
		.position = {.data = rx_positions, .n_elements = ARRAY_SIZE(rx_positions)},
		.velocity = {.data = rx_velocities, .n_elements = ARRAY_SIZE(rx_velocities)},
		.effort = {.data = rx_efforts, .n_elements = ARRAY_SIZE(rx_efforts)},
	};

	zassert_true(len > 0, "encode failed");
	zassert_true(ps_deserialize(buf, &rx, len), "decode failed");

	zassert_equal(rx.header.stamp.sec, 7, "stamp.sec");
	zassert_equal(rx.header.stamp.nanosec, 8, "stamp.nanosec");
	zassert_equal(rx.name.n_deserialized, 2, "expected 2 names, got %u",
		      rx.name.n_deserialized);
	zassert_equal(rx.position.n_deserialized, 2, "expected 2 positions");
	zassert_equal(rx.effort.n_deserialized, 0, "effort should be empty");

	zassert_str_equal(rx_names[0], "tilt_joint", "name[0]");
	zassert_str_equal(rx_names[1], "pan_joint", "name[1]");
	zassert_equal(rx_positions[0], 0.25, "position[0]");
	zassert_equal(rx_positions[1], -0.5, "position[1]");
}

/*
 * Strings deserialize in place: rstring fields point back into the caller's
 * buffer rather than into copies. The gimbal handler depends on this (it
 * strcmp()s the names before returning, and picoros frees the buffer after),
 * so it is pinned rather than assumed.
 */
ZTEST(picoserdes, test_deserialized_strings_alias_the_buffer)
{
	rstring tx_names[] = {"pan_joint"};
	double tx_positions[] = {1.0};
	double tx_velocities[] = {0.0};
	size_t len = encode_joints(0, 0, tx_names, tx_positions, tx_velocities, 1);

	rstring rx_names[2];
	double rx_positions[2];
	double rx_velocities[2];
	ros_JointState rx = {
		.name = {.data = rx_names, .n_elements = ARRAY_SIZE(rx_names)},
		.position = {.data = rx_positions, .n_elements = ARRAY_SIZE(rx_positions)},
		.velocity = {.data = rx_velocities, .n_elements = ARRAY_SIZE(rx_velocities)},
		.effort = {.data = NULL, .n_elements = 0},
	};

	zassert_true(ps_deserialize(buf, &rx, len), "decode failed");
	zassert_true((uint8_t *)rx_names[0] >= buf && (uint8_t *)rx_names[0] < buf + sizeof(buf),
		     "name[0] should point into the source buffer, not a copy");
}

/*
 * geometry_msgs/Twist, the cmd_vel payload. rmw_zenoh delivers it as six
 * float64s after the encapsulation header, which is what the previous
 * hand-rolled handler assumed by offset; this pins the same layout.
 */
ZTEST(picoserdes, test_twist_round_trip)
{
	ros_Twist tx = {
		.linear = {.x = 0.5, .y = 0.0, .z = 0.0},
		.angular = {.x = 0.0, .y = 0.0, .z = -1.25},
	};
	size_t len = ps_serialize(buf, &tx, sizeof(buf));
	ros_Twist rx = {0};

	zassert_equal(len, 52, "Twist should be 4 + 6*8 = 52 bytes, got %zu", len);
	zassert_true(ps_deserialize(buf, &rx, len), "decode failed");
	zassert_equal(rx.linear.x, 0.5, "linear.x");
	zassert_equal(rx.angular.z, -1.25, "angular.z");
}

/*
 * The reason app_picoros.c zeroes its serialization buffers before every
 * encode: Micro-CDR's ucdr_align_to() moves the iterator past CDR padding
 * without writing it, so pad bytes keep whatever the buffer held before. In a
 * reused static buffer that is a fragment of the previous message, published
 * to the network. ROS 2 subscribers ignore padding, so this is a disclosure
 * problem rather than a parsing one -- but it is also why the golden-byte
 * tests above only hold on a zeroed buffer.
 */
ZTEST(picoserdes, test_padding_is_not_zeroed_by_the_encoder)
{
	rstring names[] = {"pan_joint"};
	double positions[] = {1.0};
	double velocities[] = {-2.0};

	/* Poison the buffer, then encode *without* the memset the helpers do. */
	memset(buf, 0xAA, sizeof(buf));

	ros_JointState msg = {
		.header = {.stamp = {.sec = 1, .nanosec = 2}, .frame_id = ""},
		.name = {.data = names, .n_elements = 1},
		.position = {.data = positions, .n_elements = 1},
		.velocity = {.data = velocities, .n_elements = 1},
		.effort = {.data = NULL, .n_elements = 0},
	};
	size_t len = ps_serialize(buf, &msg, sizeof(buf));

	zassert_equal(len, sizeof(joint_golden), "length should not change");

	/* Absolute offsets 17..19 are the pad after the empty frame_id. */
	zassert_equal(buf[17], 0xAA,
		      "padding after frame_id was unexpectedly zeroed; "
		      "if picoserdes now zero-fills, the memset in "
		      "app_picoros.c can go");
}

/*
 * Not a defect to fix here, but a limitation worth pinning so nobody assumes
 * otherwise: Micro-CDR raises an error flag on its writer when the buffer runs
 * out, but ps_serialize() discards the writer and returns the truncated
 * length. There is no in-band way for a caller to tell a short message from a
 * cut-off one, which is why app_picoros.c sizes its buffers from the worst
 * case rather than checking a return code.
 */
ZTEST(picoserdes, test_serialize_overflow_is_silent)
{
	uint8_t small[32] __aligned(4);
	ros_BatteryState msg = {
		.header = {.stamp = {.sec = 0, .nanosec = 0}, .frame_id = ""},
		.voltage = 12.5f,
		.location = "",
		.serial_number = "",
	};
	size_t len = ps_serialize(small, &msg, sizeof(small));

	zassert_true(len > 0, "truncated encode still reports a length");
	zassert_true(len < sizeof(battery_golden),
		     "expected a truncated length below the full %zu bytes, got %zu",
		     sizeof(battery_golden), len);
	zassert_true(len <= sizeof(small), "wrote past the caller's buffer");
}
