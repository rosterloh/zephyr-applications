/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "prusa_camera.h"

static char buf[512];

static struct prusa_camera_info base(void)
{
	return (struct prusa_camera_info){
		.name = "cam",
		.trigger = PRUSA_CAMERA_THIRTY_SEC,
		.type = "ESP32",
		.firmware_version = "0.1.0",
		.hardware_version = "hw1",
	};
}

ZTEST(prusa_camera, test_encode_minimal_leaves_optional_keys_out)
{
	struct prusa_camera_info info = base();

	zassert_ok(prusa_camera_info_encode(&info, buf, sizeof(buf)));
	zassert_str_equal(buf, "{\"config\":{\"name\":\"cam\",\"trigger_scheme\":\"THIRTY_SEC\","
			       "\"rotation\":0},\"features\":[\"snapshot\"],"
			       "\"device\":{\"type\":\"ESP32\",\"firmware_version\":\"0.1.0\","
			       "\"hardware_version\":\"hw1\"}}");
}

ZTEST(prusa_camera, test_encode_full)
{
	struct prusa_camera_info info = base();

	info.trigger = PRUSA_CAMERA_TEN_SEC;
	info.rotation = 90;
	info.width = 1600;
	info.height = 1200;
	info.wifi_mac = "aa:bb";
	info.wifi_ipv4 = "10.0.0.2";
	info.wifi_ssid = ""; /* empty is left out too */
	info.manufacturer = "AI-Thinker";

	zassert_ok(prusa_camera_info_encode(&info, buf, sizeof(buf)));
	zassert_str_equal(buf,
			  "{\"config\":{\"name\":\"cam\",\"trigger_scheme\":\"TEN_SEC\","
			  "\"rotation\":90,\"resolution\":{\"width\":1600,\"height\":1200},"
			  "\"network_info\":{\"wifi_mac\":\"aa:bb\",\"wifi_ipv4\":\"10.0.0.2\"}},"
			  "\"features\":[\"snapshot\"],"
			  "\"device\":{\"type\":\"ESP32\",\"firmware_version\":\"0.1.0\","
			  "\"hardware_version\":\"hw1\",\"manufacturer\":\"AI-Thinker\"}}");
}

ZTEST(prusa_camera, test_encode_escapes_and_rejects)
{
	struct prusa_camera_info info = base();

	info.name = "a\"b";
	zassert_ok(prusa_camera_info_encode(&info, buf, sizeof(buf)));
	zassert_not_null(strstr(buf, "\"name\":\"a\\\"b\""), "%s", buf);

	info.name = "";
	zassert_equal(prusa_camera_info_encode(&info, buf, sizeof(buf)), -EINVAL);
	info.name = "123456789012345678901234567890123"; /* 33 */
	zassert_equal(prusa_camera_info_encode(&info, buf, sizeof(buf)), -EINVAL);

	info = base();
	info.width = 640; /* without a height */
	zassert_equal(prusa_camera_info_encode(&info, buf, sizeof(buf)), -EINVAL);

	info = base();
	zassert_true(prusa_camera_info_encode(&info, buf, 32) < 0, "overflow is an error");
}

ZTEST(prusa_camera, test_next)
{
	zassert_equal(prusa_camera_next(200), PRUSA_CAMERA_NEXT_OK);
	zassert_equal(prusa_camera_next(204), PRUSA_CAMERA_NEXT_OK);
	zassert_equal(prusa_camera_next(429), PRUSA_CAMERA_NEXT_WAIT);
	zassert_equal(prusa_camera_next(503), PRUSA_CAMERA_NEXT_WAIT);
	zassert_equal(prusa_camera_next(500), PRUSA_CAMERA_NEXT_RETRY);
	zassert_equal(prusa_camera_next(-ETIMEDOUT), PRUSA_CAMERA_NEXT_RETRY);
	zassert_equal(prusa_camera_next(-EHOSTUNREACH), PRUSA_CAMERA_NEXT_RETRY);
	zassert_equal(prusa_camera_next(400), PRUSA_CAMERA_NEXT_STOP);
	zassert_equal(prusa_camera_next(403), PRUSA_CAMERA_NEXT_STOP);
	zassert_equal(prusa_camera_next(413), PRUSA_CAMERA_NEXT_STOP);
	zassert_equal(prusa_camera_next(422), PRUSA_CAMERA_NEXT_STOP);
	zassert_equal(prusa_camera_next(-EINVAL), PRUSA_CAMERA_NEXT_STOP);
	zassert_equal(prusa_camera_next(-EMSGSIZE), PRUSA_CAMERA_NEXT_STOP);
	zassert_not_null(strstr(prusa_camera_describe(403), "15 min"));
}

ZTEST(prusa_camera, test_token)
{
	zassert_true(prusa_camera_token_valid("abcdefghij0123456789"));
	zassert_false(prusa_camera_token_valid("abcdefghij012345678"), "19");
	zassert_false(prusa_camera_token_valid("abcdefghij01234567890"), "21");
	zassert_false(prusa_camera_token_valid("abcdefghij012345678-"));
	zassert_false(prusa_camera_token_valid(""));
}

ZTEST(prusa_camera, test_trigger)
{
	zassert_equal(prusa_camera_trigger_from_seconds(10), PRUSA_CAMERA_TEN_SEC);
	zassert_equal(prusa_camera_trigger_from_seconds(600), PRUSA_CAMERA_TEN_MIN);
	zassert_equal(prusa_camera_trigger_from_seconds(45), -EINVAL);
	zassert_equal(prusa_camera_trigger_seconds(PRUSA_CAMERA_SIXTY_SEC), 60);
}

ZTEST_SUITE(prusa_camera, NULL, NULL, NULL, NULL, NULL);
