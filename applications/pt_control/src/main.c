#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#include <zephyr/app_version.h>
#include <zephyr/kernel.h>

#include "app_display.h"
#include "app_gimbal.h"
#include "app_network.h"
#include "app_time.h"
#include "app_zenoh.h"

int main(void)
{
	LOG_INF("pt_control %s", APP_VERSION_STRING);

	app_gimbal_init();
	app_display_init();

	app_net_connect();
	if (app_net_ipv4_ready()) {
		app_time_start();
		app_zenoh_init();
	} else {
		LOG_WRN("Network unavailable; skipping time sync and zenoh "
			"(store credentials with 'wifi cred add' and reboot)");
	}

	return 0;
}
