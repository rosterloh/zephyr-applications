/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * JSON-RPC-ish message layer over the vendor HID transport. Requests are
 * answered from the system workqueue: codex_rpc_handle() runs in the Bluetooth
 * RX thread and codex_hid_send() sleeps between fragments.
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "codex_hid.h"
#include "codex_json.h"
#include "codex_rpc.h"

LOG_MODULE_REGISTER(codex_rpc, LOG_LEVEL_INF);

#define FIRMWARE_VERSION "0.1.0-qtpy"

/* No fuel gauge on the QT Py; the host only uses this for its status readout. */
#define BATTERY_PERCENT 100

/* Longest request id echoed back verbatim; longer ones are answered with null. */
#define ID_MAX 24

static char pending[CODEX_JSON_MAX];

static void send_result(const char *id, const char *result)
{
	char response[256];

	if (snprintf(response, sizeof(response), "{\"id\":%s,\"result\":%s}", id, result) >=
	    (int)sizeof(response)) {
		LOG_ERR("response too long for id %s", id);
		return;
	}

	codex_hid_send(response);
}

static void handle_request(const char *json)
{
	size_t len;
	size_t id_len;
	const char *method = codex_json_member(json, "method", &len);
	const char *raw_id = codex_json_member(json, "id", &id_len);
	char id[ID_MAX] = "null";

	if (method == NULL) {
		LOG_WRN("request without a method");
		return;
	}

	if (raw_id != NULL && id_len < sizeof(id)) {
		memcpy(id, raw_id, id_len);
		id[id_len] = '\0';
	}

	if (codex_json_str_eq(method, len, "sys.version")) {
		send_result(id, "{\"version\":\"" FIRMWARE_VERSION "\"}");
		return;
	}

	if (codex_json_str_eq(method, len, "device.status")) {
		char result[128];

		snprintf(result, sizeof(result),
			 "{\"version\":\"" FIRMWARE_VERSION "\",\"profile_index\":0,"
			 "\"layer_index\":1,\"battery\":%d,\"is_charging\":false}",
			 BATTERY_PERCENT);
		send_result(id, result);
		return;
	}

	/*
	 * Lighting state is acknowledged but not rendered yet - driving the
	 * NeoKey pixels from v.oai.thstatus is the next step.
	 */
	if (codex_json_str_eq(method, len, "v.oai.thstatus") ||
	    codex_json_str_eq(method, len, "v.oai.rgbcfg") ||
	    codex_json_str_eq(method, len, "lights.preview") ||
	    codex_json_str_eq(method, len, "host.focused_app")) {
		send_result(id, "{\"ok\":true}");
		return;
	}

	LOG_WRN("unknown method %.*s", (int)len, method);

	char response[128];

	snprintf(response, sizeof(response),
		 "{\"id\":%s,\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}", id);
	codex_hid_send(response);
}

static void pending_work_handler(struct k_work *work)
{
	handle_request(pending);
	pending[0] = '\0';
}

static K_WORK_DEFINE(pending_work, pending_work_handler);

void codex_rpc_handle(const char *json)
{
	if (pending[0] != '\0') {
		LOG_WRN("dropping request, previous one still in flight");
		return;
	}

	strncpy(pending, json, sizeof(pending) - 1);
	pending[sizeof(pending) - 1] = '\0';

	k_work_submit(&pending_work);
}

void codex_rpc_send_key(const char *key, uint8_t action, int agent)
{
	char message[128];

	if (agent >= 0) {
		snprintf(
			message, sizeof(message),
			"{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"%s\",\"act\":%u,\"ag\":%d}}",
			key, action, agent);
	} else {
		snprintf(message, sizeof(message),
			 "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"%s\",\"act\":%u}}", key,
			 action);
	}

	codex_hid_send(message);
}
