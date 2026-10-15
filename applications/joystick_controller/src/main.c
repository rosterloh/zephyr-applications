#include <string.h>

#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/settings/settings.h>

#include <picoros.h>
#include <picoserdes.h>

#include "teleop.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define PERIOD_MS      (1000 / CONFIG_APP_TELEOP_PUBLISH_HZ)
#define RETRY_DELAY    K_SECONDS(2)
/* Twist is 4 (encapsulation) + 6 doubles = 52 bytes; ps_serialize cannot report
 * overflow, so this must stay a worst case.
 */
#define TWIST_BUF_SIZE 64

static atomic_t axis_x, axis_y, axis_rz, enable, l4_up;

static void input_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	switch (evt->code) {
	case INPUT_ABS_X:
		atomic_set(&axis_x, evt->value);
		break;
	case INPUT_ABS_Y:
		atomic_set(&axis_y, evt->value);
		break;
	case INPUT_ABS_RZ:
		atomic_set(&axis_rz, evt->value);
		break;
	case INPUT_BTN_0:
		atomic_set(&enable, evt->value);
		break;
	default:
		break;
	}
}
INPUT_CALLBACK_DEFINE(NULL, input_cb, NULL);

static struct net_mgmt_event_callback l4_cb;

static void l4_handler(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	atomic_set(&l4_up, event == NET_EVENT_L4_CONNECTED);
}

static picoros_node_t node = {
	.name = CONFIG_APP_PICOROS_NODE_NAME,
	.domain_id = CONFIG_APP_PICOROS_DOMAIN_ID,
};

static picoros_publisher_t pub_cmd_vel = {
	.topic =
		{
			.name = CONFIG_APP_PICOROS_CMD_VEL_TOPIC,
			.type = ROSTYPE_NAME(ros_Twist),
			.rihs_hash = ROSTYPE_HASH(ros_Twist),
		},
};

/* ps_serialize writes the CDR header as a uint32_t store: Xtensa faults on unaligned. */
static uint8_t twist_buf[TWIST_BUF_SIZE] __aligned(4);

static bool ros_connect(void)
{
	picoros_interface_t ifx = {
		.mode = "client",
		.locator = CONFIG_APP_PICOROS_LOCATOR,
	};

	if (picoros_interface_init(&ifx) != PICOROS_OK) {
		LOG_WRN("router %s unreachable", ifx.locator);
		return false;
	}
	if (picoros_node_init(&node) != PICOROS_OK) {
		LOG_ERR("node init failed");
		picoros_interface_close();
		return false;
	}
	if (picoros_publisher_declare(&node, &pub_cmd_vel) != PICOROS_OK) {
		LOG_ERR("publisher declare failed for '%s'", CONFIG_APP_PICOROS_CMD_VEL_TOPIC);
		picoros_node_drop(&node);
		picoros_interface_close();
		return false;
	}

	LOG_INF("publishing Twist on /%s", CONFIG_APP_PICOROS_CMD_VEL_TOPIC);
	return true;
}

static void ros_disconnect(void)
{
	picoros_publisher_drop(&pub_cmd_vel);
	picoros_node_drop(&node);
	picoros_interface_close();
}

static bool publish(const struct teleop_twist *t)
{
	ros_Twist msg = {
		.linear = {.x = t->linear_x, .y = t->linear_y},
		.angular = {.z = t->angular_z},
	};

	/* Micro-CDR skips padding without writing it; zero so no stale bytes leak. */
	memset(twist_buf, 0, sizeof(twist_buf));
	size_t len = ps_serialize(twist_buf, &msg, sizeof(twist_buf));

	return len > 0 && picoros_publish(&pub_cmd_vel, twist_buf, len) == PICOROS_OK;
}

int main(void)
{
	const struct teleop_limits lim = {
		.max_linear = CONFIG_APP_TELEOP_MAX_LINEAR_MM_S / 1000.0,
		.max_angular = CONFIG_APP_TELEOP_MAX_ANGULAR_MRAD_S / 1000.0,
	};
	struct teleop_state state = {0};
	bool connected = false;

	settings_subsys_init();
	settings_load();

	net_mgmt_init_event_callback(&l4_cb, l4_handler,
				     NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);
	net_mgmt_add_event_callback(&l4_cb);
	conn_mgr_mon_resend_status();
	conn_mgr_all_if_connect(true);

	for (;;) {
		if (!connected) {
			teleop_reset(&state);
			if (atomic_get(&l4_up) && ros_connect()) {
				connected = true;
			} else {
				k_sleep(RETRY_DELAY);
			}
			continue;
		}

		const struct teleop_input in = {
			.enable = atomic_get(&enable),
			.x = atomic_get(&axis_x),
			.y = atomic_get(&axis_y),
			.rz = atomic_get(&axis_rz),
		};
		struct teleop_twist out;

		if (!atomic_get(&l4_up) || !picoros_interface_is_up() ||
		    (teleop_step(&state, &in, &lim, &out) && !publish(&out))) {
			LOG_WRN("link lost, reconnecting");
			ros_disconnect();
			connected = false;
			continue;
		}

		k_msleep(PERIOD_MS);
	}

	return 0;
}
