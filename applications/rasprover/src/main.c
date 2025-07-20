#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include "app_display.h"
#include "app_network.h"
#include "app_settings.h"
#include "app_sensors.h"
#include <golioth/client.h>
#include <golioth/fw_update.h>
#include <zephyr/kernel.h>

/* Current firmware version; update in VERSION */
static const char *_current_version =
	STRINGIFY(APP_VERSION_MAJOR) "." STRINGIFY(APP_VERSION_MINOR) "." STRINGIFY(APP_PATCHLEVEL);

static struct golioth_client *client;
K_SEM_DEFINE(connected, 0, 1);

static k_tid_t _system_thread = 0;

void wake_system_thread(void)
{
	k_wakeup(_system_thread);
}

static void on_client_event(struct golioth_client *client, enum golioth_client_event event,
			    void *arg)
{
	bool is_connected = (event == GOLIOTH_CLIENT_EVENT_CONNECTED);

	if (is_connected) {
		k_sem_give(&connected);
		// golioth_connection_led_set(1);
	}
	LOG_INF("Golioth client %s", is_connected ? "connected" : "disconnected");
}

static void start_golioth_client(void)
{
	/* Get the client configuration from auto-loaded settings */
	const struct golioth_client_config *client_config = golioth_app_credentials_get();

	/* Create and start a Golioth Client */
	client = golioth_client_create(client_config);

	/* Register Golioth on_connect callback */
	golioth_client_register_event_callback(client, on_client_event, NULL);

	/* Initialise DFU components */
	golioth_fw_update_init(client, _current_version);

	/* Observe State service data */
	// app_state_observe(client);

	/* Set Golioth Client for streaming sensor data */
	app_sensors_set_client(client);

	/* Register Settings service */
	app_settings_register(client);

	/* Register RPC service */
	// app_rpc_register(client);
}

int main(void)
{
	// int err;
	LOG_INF("Firmware version: %s", _current_version);

	/* Get system thread id so loop delay change event can wake main */
	_system_thread = k_current_get();

	/* Run WiFi/DHCP if necessary */
	// app_net_connect();

	/* Start Golioth client */
	// start_golioth_client();

	/* Block until connected to Golioth */
	// k_sem_take(&connected, K_FOREVER);

	app_sensors_init();

#ifdef CONFIG_APP_DISPLAY
	// app_display_init();
#endif /* CONFIG_APP_DISPLAY */

	while (true) {
		// app_sensors_read_and_stream();

		k_sleep(K_SECONDS(get_loop_delay_s()));
	}
}