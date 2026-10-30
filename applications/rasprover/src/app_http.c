/*
 * Read-only status page: GET / is a page that polls GET /status.json.
 *
 * The server runs every callback on its own thread at the lowest preemptible
 * priority, one callback at a time, so the single response buffer is safe.
 * Everything it reports is a snapshot or a locked driver read; nothing here
 * can command the robot.
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_http, LOG_LEVEL_INF);

#include <zephyr/app_version.h>
#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>

#include "app_gimbal.h"
#include "app_http.h"
#include "app_motors.h"
#include "app_network.h"
#include "app_sensors.h"
#include "app_time.h"

static uint16_t http_port = CONFIG_APP_HTTP_PORT;
HTTP_SERVICE_DEFINE(rasprover_http, NULL, &http_port, CONFIG_HTTP_SERVER_MAX_CLIENTS, 1, NULL, NULL,
		    NULL);

static const char index_html[] =
	"<!doctype html><meta charset=utf-8><title>rasprover</title>"
	"<pre id=s>loading</pre><script>"
	"setInterval(async()=>{try{s.textContent=JSON.stringify("
	"await(await fetch('/status.json')).json(),null,2)}catch(e){s.textContent=e}},1000)"
	"</script>";

static struct http_resource_detail_static index_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "text/html",
		},
	.static_data = index_html,
	.static_data_len = sizeof(index_html) - 1,
};

/* Worst case with int32-sized values is ~540 bytes. */
static char body[640];

#define MILLI(x) ((long long)((x) * 1000.0))

static size_t append(size_t len, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (len >= sizeof(body)) {
		return len;
	}
	va_start(ap, fmt);
	n = vsnprintk(body + len, sizeof(body) - len, fmt, ap);
	va_end(ap);
	return len + MAX(n, 0);
}

/* "name":[{...},{...}] or "name":null when the joints can't be read. */
#define APPEND_JOINTS(len, key, joints, ok)                                                        \
	do {                                                                                       \
		if (!(ok)) {                                                                       \
			len = append(len, ",\"" key "\":null");                                    \
			break;                                                                     \
		}                                                                                  \
		len = append(len, ",\"" key "\":[");                                               \
		ARRAY_FOR_EACH(joints, j) {                                                        \
			len = append(len,                                                          \
				     "%s{\"name\":\"%s\",\"position_mrad\":%lld,"                  \
				     "\"velocity_mrad_s\":%lld}",                                  \
				     j ? "," : "", joints[j].name, MILLI(joints[j].position_rad),  \
				     MILLI(joints[j].velocity_rad_s));                             \
		}                                                                                  \
		len = append(len, "]");                                                            \
	} while (0)

static size_t build_status(void)
{
	struct app_power pw;
	size_t len;

	len = append(0, "{\"version\":\"%s\",\"uptime_ms\":%lld,\"net\":%s,\"time_synced\":%s",
		     APP_VERSION_STRING, k_uptime_get(), app_net_ipv4_ready() ? "true" : "false",
		     app_time_synced() ? "true" : "false");

	if (app_sensors_last_power(&pw)) {
		len = append(len,
			     ",\"power\":{\"voltage_mv\":%d,\"current_ma\":%d,\"power_mw\":%d,"
			     "\"age_ms\":%lld}",
			     pw.voltage_mv, pw.current_ma, pw.power_mw,
			     k_uptime_get() - pw.uptime_ms);
	} else {
		len = append(len, ",\"power\":null");
	}

#ifdef CONFIG_APP_MOTORS
	struct app_motors_joint_state wheels[APP_MOTORS_JOINT_COUNT];

	APPEND_JOINTS(len, "wheels", wheels, app_motors_read_joint_state(wheels));
#endif
#ifdef CONFIG_APP_GIMBAL
	struct app_gimbal_joint_state gimbal[APP_GIMBAL_JOINT_COUNT];

	APPEND_JOINTS(len, "gimbal", gimbal, app_gimbal_read_joint_state(gimbal));
#endif

	return append(len, "}");
}

static int status_handler(struct http_client_ctx *client, enum http_transaction_status status,
			  const struct http_request_ctx *req, struct http_response_ctx *rsp,
			  void *user_data)
{
	size_t len;

	ARG_UNUSED(client);
	ARG_UNUSED(req);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	len = build_status();
	if (len >= sizeof(body)) {
		LOG_ERR("status.json truncated");
		return -ENOMEM;
	}

	rsp->body = (const uint8_t *)body;
	rsp->body_len = len;
	rsp->final_chunk = true;
	return 0;
}

static struct http_resource_detail_dynamic status_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "application/json",
		},
	.cb = status_handler,
};

HTTP_RESOURCE_DEFINE(index_res, rasprover_http, "/", &index_detail);
HTTP_RESOURCE_DEFINE(status_res, rasprover_http, "/status.json", &status_detail);

int app_http_start(void)
{
	int ret = http_server_start();

	if (ret != 0) {
		LOG_ERR("http server start failed: %d", ret);
		return ret;
	}

	LOG_INF("status page on port %u", http_port);
	return 0;
}
