#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/app_version.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/actuator/actuator.h>
#include <zephyr/actuator/actuator_group.h>
#include <zephyr/net/mcp/mcp_server.h>
#include <zephyr/net/mcp/mcp_server_http.h>

#include <drivers/bus_servo.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pt_mcp, LOG_LEVEL_INF);

#define SERVO_BUS_NODE DT_NODELABEL(servo_bus)

static const struct device *const pan = DEVICE_DT_GET(DT_ALIAS(pan_servo));
static const struct device *const tilt = DEVICE_DT_GET(DT_ALIAS(tilt_servo));

/* Group so a combined pan+tilt setpoint is issued as one atomic bus write
 * (native sync-write) rather than two back-to-back single-servo writes, which
 * the bus servos can drop. Index order: [0]=pan, [1]=tilt.
 */
ACTUATOR_GROUP_DEFINE(pan_tilt, DEVICE_DT_GET(DT_ALIAS(pan_servo)),
		      DEVICE_DT_GET(DT_ALIAS(tilt_servo)));

static mcp_server_ctx_t server;
static bool motors_ready;

/* Minimal extractors for the flat JSON argument objects MCP hands us. */
static const char *json_value_start(const char *json, const char *key)
{
	char pattern[16];
	const char *pos;

	if (json == NULL) {
		return NULL;
	}

	snprintk(pattern, sizeof(pattern), "\"%s\"", key);
	pos = strstr(json, pattern);
	if (pos == NULL) {
		return NULL;
	}

	pos = strchr(pos + strlen(pattern), ':');
	if (pos == NULL) {
		return NULL;
	}
	pos++;

	while ((*pos == ' ') || (*pos == '\t') || (*pos == '\n') || (*pos == '\r')) {
		pos++;
	}

	return pos;
}

static bool json_get_number(const char *json, const char *key, float *out)
{
	const char *start = json_value_start(json, key);
	char *end;
	float value;

	if (start == NULL) {
		return false;
	}

	value = strtof(start, &end);
	if (end == start) {
		return false;
	}

	*out = value;
	return true;
}

static bool json_string_equals(const char *json, const char *key, const char *expected)
{
	const char *start = json_value_start(json, key);
	size_t len = strlen(expected);

	if ((start == NULL) || (*start != '"')) {
		return false;
	}

	return (strncmp(start + 1, expected, len) == 0) && (start[1 + len] == '"');
}

static int submit_text(const char *text, const char *execution_token)
{
	struct mcp_tool_message response = {
		.type = MCP_USR_TOOL_RESPONSE,
		.data = (char *)text,
		.length = strlen(text),
	};

	return mcp_server_submit_tool_message(server, &response, execution_token);
}

static int motor_control_tool_callback(enum mcp_tool_event_type event, const char *arguments,
				       const char *execution_token)
{
	char buf[128];
	int ret;

	if (event == MCP_TOOL_CANCEL_REQUEST) {
		struct mcp_tool_message cancel_ack = {
			.type = MCP_USR_TOOL_CANCEL_ACK,
			.data = NULL,
			.length = 0,
		};

		mcp_server_submit_tool_message(server, &cancel_ack, execution_token);
		return 0;
	}

	if (!motors_ready) {
		submit_text("Motors not initialized", execution_token);
		return -ENODEV;
	}

	if (json_string_equals(arguments, "action", "get")) {
		struct actuator_feedback pan_fb;
		struct actuator_feedback tilt_fb;

		if ((actuator_read_feedback(pan, &pan_fb) != 0) ||
		    !(pan_fb.valid_mask & ACTUATOR_FB_POSITION) ||
		    (actuator_read_feedback(tilt, &tilt_fb) != 0) ||
		    !(tilt_fb.valid_mask & ACTUATOR_FB_POSITION)) {
			submit_text("Failed to read motor positions", execution_token);
			return -EIO;
		}

		snprintk(buf, sizeof(buf), "{\"pan\":%.3f,\"tilt\":%.3f}", (double)pan_fb.position,
			 (double)tilt_fb.position);
	} else if (json_string_equals(arguments, "action", "set")) {
		float pan_rad;
		float tilt_rad;
		bool have_pan = json_get_number(arguments, "pan", &pan_rad);
		bool have_tilt = json_get_number(arguments, "tilt", &tilt_rad);

		if (!have_pan && !have_tilt) {
			submit_text("Set requires \"pan\" and/or \"tilt\" (radians)",
				    execution_token);
			return -EINVAL;
		}

		if (have_pan && have_tilt) {
			float pos[2] = {pan_rad, tilt_rad};

			ret = actuator_group_set_position(&pan_tilt, pos);
		} else if (have_pan) {
			ret = actuator_set_position(pan, pan_rad);
		} else {
			ret = actuator_set_position(tilt, tilt_rad);
		}
		if (ret != 0) {
			snprintk(buf, sizeof(buf), "Motor setpoint failed: %d", ret);
			submit_text(buf, execution_token);
			return ret;
		}

		ret = snprintk(buf, sizeof(buf), "Moving:");
		if (have_pan) {
			ret += snprintk(buf + ret, sizeof(buf) - ret, " pan=%.3f", (double)pan_rad);
		}
		if (have_tilt) {
			snprintk(buf + ret, sizeof(buf) - ret, " tilt=%.3f", (double)tilt_rad);
		}
	} else {
		submit_text("Invalid action. Use: get or set", execution_token);
		return -EINVAL;
	}

	return submit_text(buf, execution_token);
}

static const struct mcp_tool_record motor_control_tool = {
	.metadata =
		{
			.name = "motor_control",
			.input_schema = "{"
					"\"type\":\"object\","
					"\"properties\":{"
					"\"action\":{"
					"\"type\":\"string\","
					"\"enum\":[\"get\",\"set\"],"
					"\"description\":\"Read or command the motor positions\""
					"},"
					"\"pan\":{"
					"\"type\":\"number\","
					"\"description\":\"Pan setpoint in radians (set only)\""
					"},"
					"\"tilt\":{"
					"\"type\":\"number\","
					"\"description\":\"Tilt setpoint in radians (set only)\""
					"}"
					"},"
					"\"required\":[\"action\"]"
					"}",
#ifdef CONFIG_MCP_TOOL_DESC
			.description = "Gets or sets the pan/tilt motor positions in radians",
#endif
#ifdef CONFIG_MCP_TOOL_TITLE
			.title = "Motor Control Tool",
#endif
		},
	.callback = motor_control_tool_callback,
};

static bool motors_init(void)
{
	struct bus_servo_iface_param param = {
		.rx_timeout_us = 50000,
		.serial =
			{
				.baud = DT_PROP(DT_PARENT(SERVO_BUS_NODE), current_speed),
				.parity = UART_CFG_PARITY_NONE,
			},
	};
	int iface = bus_servo_iface_get_by_name(DEVICE_DT_NAME(SERVO_BUS_NODE));

	if ((iface < 0) || (bus_servo_init(iface, param) != 0)) {
		LOG_ERR("servo bus init failed");
		return false;
	}

	if (!device_is_ready(pan) || !device_is_ready(tilt)) {
		LOG_ERR("servo devices not ready");
		return false;
	}

	if ((actuator_enable(pan) != 0) || (actuator_enable(tilt) != 0)) {
		LOG_ERR("servo enable failed");
		return false;
	}

	LOG_INF("pan/tilt motors ready");
	return true;
}

int main(void)
{
	int ret;

	LOG_INF("pt_mcp %s", APP_VERSION_STRING);

	motors_ready = motors_init();
	if (!motors_ready) {
		LOG_WRN("motor_control tool will report errors until motors are available");
	}

	server = mcp_server_init();
	if (server == NULL) {
		LOG_ERR("MCP server initialization failed");
		return -ENOMEM;
	}

	ret = mcp_server_http_init(server);
	if (ret != 0) {
		LOG_ERR("MCP HTTP server initialization failed: %d", ret);
		return ret;
	}

	ret = mcp_server_add_tool(server, &motor_control_tool);
	if (ret != 0) {
		LOG_ERR("motor_control tool registration failed: %d", ret);
		return ret;
	}

	ret = mcp_server_start(server);
	if (ret != 0) {
		LOG_ERR("MCP server start failed: %d", ret);
		return ret;
	}

	ret = mcp_server_http_start(server);
	if (ret != 0) {
		LOG_ERR("MCP HTTP server start failed: %d", ret);
		return ret;
	}

	LOG_INF("MCP server running on port %d%s", CONFIG_MCP_HTTP_PORT, CONFIG_MCP_HTTP_ENDPOINT);

	return 0;
}
