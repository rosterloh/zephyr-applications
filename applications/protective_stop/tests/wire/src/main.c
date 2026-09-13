/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Golden-byte tests for the pstop wire format.
 *
 * Why golden bytes: a wrong field offset does not fail the build, it fails at
 * the machine, which rejects the frame on checksum and silently never bonds.
 * These arrays were produced by upstream's OWN encoder (pstop_c @ 4ce8094)
 * compiled on the host, so they pin the format as the machine will read it,
 * independently of whatever our build happens to do.
 *
 * Layout being pinned (little-endian, 48 bytes):
 *   [0]     version u8          [1]     message u8
 *   [2:10]  stamp u64           [10:18] received_stamp u64
 *   [18:22] id u32              [22:26] receiver_id u32
 *   [26:30] heartbeat_timeout   [30:34] counter u32
 *   [34:38] received_counter    [38:42] padding1  [42:46] padding2
 *   [46:48] crc16 over [0:46], poly 0x8D95, init 0xFFFF
 */

#include <string.h>
#include <zephyr/ztest.h>

#include "pstop/device_id.h"
#include "pstop/pstop_msg.h"

#define ME_ID      0xC0A80105U /* 192.168.1.5 */
#define MACHINE_ID 0x01020304U

static const uint8_t golden_bond[PSTOP_MESSAGE_SIZE] = {
	0x02, 0xAD, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x01, 0xA8, 0xC0, 0x04, 0x03,
	0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x65, 0x2A,
};

static const uint8_t golden_ok[PSTOP_MESSAGE_SIZE] = {
	0x02, 0x55, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x01, 0xA8, 0xC0, 0x04, 0x03,
	0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x05, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x48,
};

static const uint8_t golden_stop[PSTOP_MESSAGE_SIZE] = {
	0x02, 0x92, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2F,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x01, 0xA8, 0xC0, 0x04, 0x03,
	0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB1, 0x66,
};

ZTEST(pstop_wire, test_message_size_is_48)
{
	zassert_equal(PSTOP_MESSAGE_SIZE, 48U, "wire size must be 48");
}

ZTEST(pstop_wire, test_encode_bond_matches_golden)
{
	device_id_t me = {.data = ME_ID};
	device_id_t machine = {.data = MACHINE_ID};
	pstop_msg_t msg;
	uint8_t buf[PSTOP_MESSAGE_SIZE];

	pstop_create_bond_message(&msg, 0x1234ULL, &me, &machine, 1U);
	pstop_message_encode(&msg, buf);

	zassert_mem_equal(buf, golden_bond, PSTOP_MESSAGE_SIZE, "BOND frame diverged");
}

ZTEST(pstop_wire, test_encode_ok_matches_golden)
{
	device_id_t me = {.data = ME_ID};
	device_id_t machine = {.data = MACHINE_ID};
	pstop_msg_t msg;
	uint8_t buf[PSTOP_MESSAGE_SIZE];

	pstop_create_ok_message(&msg, 0x2000ULL, 0x1F00ULL, &me, &machine, 7U, 5U);
	pstop_message_encode(&msg, buf);

	zassert_mem_equal(buf, golden_ok, PSTOP_MESSAGE_SIZE, "OK frame diverged");
}

ZTEST(pstop_wire, test_encode_stop_matches_golden)
{
	device_id_t me = {.data = ME_ID};
	device_id_t machine = {.data = MACHINE_ID};
	pstop_msg_t msg;
	uint8_t buf[PSTOP_MESSAGE_SIZE];

	pstop_create_stop_message(&msg, 0x3000ULL, 0x2F00ULL, &me, &machine, 8U, 6U);
	pstop_message_encode(&msg, buf);

	zassert_mem_equal(buf, golden_stop, PSTOP_MESSAGE_SIZE, "STOP frame diverged");
}

ZTEST(pstop_wire, test_decode_golden_ok_recovers_fields)
{
	pstop_msg_t msg;

	pstop_message_decode(&msg, golden_ok);

	zassert_equal(msg.version, 0x02U, "version");
	zassert_equal(msg.message, PSTOP_MESSAGE_OK, "message type");
	zassert_equal(msg.stamp, 0x2000ULL, "stamp");
	zassert_equal(msg.received_stamp, 0x1F00ULL, "received_stamp");
	zassert_equal(msg.id.data, ME_ID, "sender id");
	zassert_equal(msg.receiver_id.data, MACHINE_ID, "receiver id");
	zassert_equal(msg.counter, 7U, "counter");
	zassert_equal(msg.received_counter, 5U, "received_counter");
	zassert_equal(msg.checksum, msg.calculated_checksum, "crc must self-verify");
}

ZTEST(pstop_wire, test_corrupt_byte_breaks_checksum)
{
	uint8_t buf[PSTOP_MESSAGE_SIZE];
	pstop_msg_t msg;

	memcpy(buf, golden_ok, PSTOP_MESSAGE_SIZE);
	buf[30] ^= 0x01U; /* flip a counter bit */
	pstop_message_decode(&msg, buf);

	zassert_not_equal(msg.checksum, msg.calculated_checksum,
			  "a flipped payload bit must break the crc");
}

ZTEST_SUITE(pstop_wire, NULL, NULL, NULL, NULL, NULL);
