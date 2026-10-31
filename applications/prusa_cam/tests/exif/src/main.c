/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "exif.h"

static const uint8_t jpeg[] = {0xFF, 0xD8, 0xFF, 0xDB, 0x00, 0x43};

ZTEST(exif, test_orientation_map)
{
	zassert_equal(exif_orientation(0), 1);
	zassert_equal(exif_orientation(1), 6, "90 deg clockwise");
	zassert_equal(exif_orientation(2), 3);
	zassert_equal(exif_orientation(3), 8);
	zassert_equal(exif_orientation(4), 1, "out of range is upright");
}

ZTEST(exif, test_header_layout)
{
	uint8_t h[EXIF_HDR_LEN];

	exif_header(h, 6);

	zassert_mem_equal(h, "\xFF\xD8\xFF\xE1", 4, "SOI then APP1");
	/* APP1 length counts itself and the payload, not the marker or SOI. */
	zassert_equal((h[4] << 8) | h[5], EXIF_HDR_LEN - 4);
	zassert_mem_equal(&h[6], "Exif\0\0MM\0\x2A\0\0\0\x08", 14);
	zassert_equal((h[20] << 8) | h[21], 1, "one IFD0 entry");
	zassert_equal((h[22] << 8) | h[23], 0x0112, "Orientation tag");
	zassert_equal((h[24] << 8) | h[25], 3, "SHORT");
	zassert_equal((h[30] << 8) | h[31], 6);
	zassert_mem_equal(&h[34], "\0\0\0\0", 4, "no next IFD");
}

ZTEST(exif, test_wanted)
{
	static const uint8_t not_jpeg[] = {0x00, 0xD8, 0xFF};

	zassert_false(exif_wanted(jpeg, sizeof(jpeg), 0), "upright needs no tag");
	zassert_true(exif_wanted(jpeg, sizeof(jpeg), 1));
	zassert_false(exif_wanted(jpeg, sizeof(jpeg), 4));
	zassert_false(exif_wanted(not_jpeg, sizeof(not_jpeg), 1), "no SOI");
	zassert_false(exif_wanted(jpeg, 1, 1), "too short");
}

ZTEST_SUITE(exif, NULL, NULL, NULL, NULL, NULL);
