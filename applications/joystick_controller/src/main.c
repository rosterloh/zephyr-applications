#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/settings/settings.h>

#include <picoros.h>
#include <picoserdes.h>

#include "app_time.h"
#include "teleop.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define PERIOD_MS   (1000 / CONFIG_APP_TELEOP_PUBLISH_HZ)
#define RETRY_DELAY K_SECONDS(2)
#define ARM         IS_ENABLED(CONFIG_APP_TELEOP_TARGET_ARM)
/* ps_serialize cannot report overflow, so this must stay a worst case. Twist is
 * 4 (encapsulation) + 6 doubles = 52 bytes. TwistStamped adds the header before
 * the doubles: stamp 8, frame_id length 4 plus its bytes and NUL, then up to 7
 * of padding to align the first double: 71 + sizeof(frame_id).
 */
#ifdef CONFIG_APP_TELEOP_TARGET_ARM
#define MSG_BUF_SIZE (72 + sizeof(CONFIG_APP_PICOROS_SERVO_FRAME_ID))
#else
#define MSG_BUF_SIZE 64
#endif

static atomic_t axis_x, axis_y, axis_rz, l4_up;

#define USER_NODE DT_PATH(zephyr_user)
static const struct gpio_dt_spec enable_sw = GPIO_DT_SPEC_GET(USER_NODE, enable_gpios);
/* Optional: a board without them never shifts the stick or grasps. */
static const struct gpio_dt_spec mod_sw = GPIO_DT_SPEC_GET_OR(USER_NODE, mod_gpios, {0});
static const struct gpio_dt_spec grasp_sw = GPIO_DT_SPEC_GET_OR(USER_NODE, grasp_gpios, {0});

static int switch_init(const struct gpio_dt_spec *sw)
{
	if (sw->port == NULL) {
		return 0;
	}
	if (!gpio_is_ready_dt(sw)) {
		return -ENODEV;
	}
	return gpio_pin_configure_dt(sw, GPIO_INPUT);
}

/* Absent, or a failed read (e.g. seesaw I2C gone), counts as off. */
static bool switch_on(const struct gpio_dt_spec *sw)
{
	return sw->port != NULL && gpio_pin_get_dt(sw) > 0;
}

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

/* ROSTYPE_NAME token-pastes its argument, so the type can't come from a macro. */
#ifdef CONFIG_APP_TELEOP_TARGET_ARM
static picoros_publisher_t pub_twist = {
	.topic =
		{
			.name = CONFIG_APP_PICOROS_SERVO_TOPIC,
			.type = ROSTYPE_NAME(ros_TwistStamped),
			.rihs_hash = ROSTYPE_HASH(ros_TwistStamped),
		},
};

static picoros_publisher_t pub_grasp = {
	.topic =
		{
			.name = CONFIG_APP_PICOROS_GRASP_TOPIC,
			.type = ROSTYPE_NAME(ros_Bool),
			.rihs_hash = ROSTYPE_HASH(ros_Bool),
		},
};

static picoros_publisher_t *const pubs[] = {&pub_twist, &pub_grasp};
#else
static picoros_publisher_t pub_twist = {
	.topic =
		{
			.name = CONFIG_APP_PICOROS_CMD_VEL_TOPIC,
			.type = ROSTYPE_NAME(ros_Twist),
			.rihs_hash = ROSTYPE_HASH(ros_Twist),
		},
};

static picoros_publisher_t *const pubs[] = {&pub_twist};
#endif

/* ps_serialize writes the CDR header as a uint32_t store: Xtensa faults on unaligned. */
static uint8_t msg_buf[MSG_BUF_SIZE] __aligned(4);

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
	for (size_t i = 0; i < ARRAY_SIZE(pubs); i++) {
		if (picoros_publisher_declare(&node, pubs[i]) != PICOROS_OK) {
			LOG_ERR("publisher declare failed for '%s'", pubs[i]->topic.name);
			while (i-- > 0) {
				picoros_publisher_drop(pubs[i]);
			}
			picoros_node_drop(&node);
			picoros_interface_close();
			return false;
		}
		LOG_INF("publishing on /%s", pubs[i]->topic.name);
	}

	return true;
}

static void ros_disconnect(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(pubs); i++) {
		picoros_publisher_drop(pubs[i]);
	}
	picoros_node_drop(&node);
	picoros_interface_close();
}

static bool publish_buf(picoros_publisher_t *pub, size_t len)
{
	return len > 0 && picoros_publish(pub, msg_buf, len) == PICOROS_OK;
}

static bool publish_twist(const struct teleop_twist *t)
{
	ros_Twist twist = {
		.linear = {.x = t->linear_x, .y = t->linear_y, .z = t->linear_z},
		.angular = {.x = t->angular_x, .y = t->angular_y, .z = t->angular_z},
	};

	/* Micro-CDR skips padding without writing it; zero so no stale bytes leak. */
	memset(msg_buf, 0, sizeof(msg_buf));
#ifdef CONFIG_APP_TELEOP_TARGET_ARM
	const struct app_ros_time now = app_time_now();
	ros_TwistStamped msg = {
		.header =
			{
				.stamp = {.sec = (int32_t)now.sec, .nanosec = now.nanosec},
				/* rstring is char*; picoserdes only reads it when serializing. */
				.frame_id = (rstring)CONFIG_APP_PICOROS_SERVO_FRAME_ID,
			},
		.twist = twist,
	};

	if (!app_time_synced()) {
		LOG_WRN_ONCE("no SNTP time yet: Servo drops unstamped commands");
	}
	return publish_buf(&pub_twist, ps_serialize(msg_buf, &msg, sizeof(msg_buf)));
#else
	return publish_buf(&pub_twist, ps_serialize(msg_buf, &twist, sizeof(msg_buf)));
#endif
}

#ifdef CONFIG_APP_TELEOP_TARGET_ARM
static bool publish_grasp(bool grasp)
{
	memset(msg_buf, 0, sizeof(msg_buf));
	return publish_buf(&pub_grasp, ps_serialize(msg_buf, &grasp, sizeof(msg_buf)));
}
#endif

int main(void)
{
	const struct teleop_limits lim = {
		.max_linear = CONFIG_APP_TELEOP_MAX_LINEAR_MM_S / 1000.0,
		.max_angular = CONFIG_APP_TELEOP_MAX_ANGULAR_MRAD_S / 1000.0,
	};
	struct teleop_state state;
	bool connected = false;

	teleop_reset(&state);
	if (switch_init(&enable_sw) || switch_init(&mod_sw) || switch_init(&grasp_sw)) {
		LOG_ERR("enable, mod or grasp switch unavailable");
		return -ENODEV;
	}

	settings_subsys_init();
	settings_load();

	net_mgmt_init_event_callback(&l4_cb, l4_handler,
				     NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);
	net_mgmt_add_event_callback(&l4_cb);
	conn_mgr_mon_resend_status();
	conn_mgr_all_if_connect(true);
#ifdef CONFIG_NET_L2_ETHERNET
	/* The Wi-Fi driver starts DHCP on association; nothing does for Ethernet. */
	struct net_if *eth = net_if_get_first_by_type(&NET_L2_GET_NAME(ETHERNET));

	if (eth != NULL) {
		net_dhcpv4_start(eth);
	}
#endif
	app_time_start();

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
			.enable = switch_on(&enable_sw),
			.mod = ARM && switch_on(&mod_sw),
			.grasp = ARM && switch_on(&grasp_sw),
			.x = atomic_get(&axis_x),
			.y = atomic_get(&axis_y),
			.rz = atomic_get(&axis_rz),
		};
		struct teleop_twist out;
		bool ok = atomic_get(&l4_up) && picoros_interface_is_up();

		if (ok && teleop_step(&state, &in, &lim, &out)) {
			ok = publish_twist(&out);
		}
#ifdef CONFIG_APP_TELEOP_TARGET_ARM
		bool grasp;

		if (ok && teleop_grasp_step(&state, &in, &grasp)) {
			ok = publish_grasp(grasp);
		}
#endif
		if (!ok) {
			LOG_WRN("link lost, reconnecting");
			ros_disconnect();
			connected = false;
			continue;
		}

		k_msleep(PERIOD_MS);
	}

	return 0;
}
