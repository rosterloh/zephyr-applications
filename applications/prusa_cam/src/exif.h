/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * A minimal Exif APP1 segment carrying only the Orientation tag, so viewers
 * (Prusa Connect, browsers) show a rotated camera upright without the
 * firmware re-encoding the JPEG. Header-only so it can be tested on
 * native_sim.
 */

#ifndef PRUSA_CAM_EXIF_H_
#define PRUSA_CAM_EXIF_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* SOI followed by the APP1 segment; replaces the frame's own SOI. */
#define EXIF_HDR_LEN 38

/* Exif Orientation for a clockwise rotation index 0..3 (0/90/180/270 deg). */
static inline uint16_t exif_orientation(uint8_t rotation)
{
	static const uint16_t orient[] = {1, 6, 3, 8};

	return rotation < 4 ? orient[rotation] : 1;
}

static inline void exif_header(uint8_t out[EXIF_HDR_LEN], uint16_t orientation)
{
	static const uint8_t tmpl[EXIF_HDR_LEN] = {
		0xFF, 0xD8,                                     /* SOI */
		0xFF, 0xE1, 0x00, 0x22,                         /* APP1, 34 bytes */
		'E',  'x',  'i',  'f',  0x00, 0x00,             /* Exif identifier */
		'M',  'M',  0x00, 0x2A, 0x00, 0x00, 0x00, 0x08, /* big-endian TIFF, IFD0 at 8 */
		0x00, 0x01,                                     /* one IFD0 entry */
		0x01, 0x12, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, /* Orientation, SHORT, count 1 */
		0x00, 0x00, 0x00, 0x00,                         /* value, patched below */
		0x00, 0x00, 0x00, 0x00,                         /* no IFD1 */
	};

	memcpy(out, tmpl, sizeof(tmpl));
	out[30] = orientation >> 8;
	out[31] = orientation & 0xFF;
}

/*
 * Whether to send exif_header() + jpeg[2..] instead of jpeg: only for a
 * rotated image, and only if the frame starts with SOI.
 */
static inline bool exif_wanted(const uint8_t *jpeg, size_t len, uint8_t rotation)
{
	return rotation != 0 && rotation < 4 && len >= 2 && jpeg[0] == 0xFF && jpeg[1] == 0xD8;
}

#endif /* PRUSA_CAM_EXIF_H_ */
