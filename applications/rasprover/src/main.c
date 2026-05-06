#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include "app_display.h"
#include "app_network.h"
#include "app_sensors.h"
#include "app_settings.h"
#include "app_zenoh.h"
#include <zephyr/kernel.h>

static k_tid_t _system_thread = 0;

void wake_system_thread(void)
{
	k_wakeup(_system_thread);
}

int main(void)
{
	LOG_INF("Firmware version: %s",
		STRINGIFY(APP_VERSION_MAJOR) "." STRINGIFY(APP_VERSION_MINOR) "."
		STRINGIFY(APP_PATCHLEVEL));

	_system_thread = k_current_get();

	app_sensors_init();
	app_net_connect();
	app_zenoh_init();

#ifdef CONFIG_APP_DISPLAY
	app_display_init();
#endif

	while (true) {
		app_sensors_read_and_stream();

		k_sleep(K_SECONDS(get_loop_delay_s()));
	}
}
