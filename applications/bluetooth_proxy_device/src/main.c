#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <app_version.h>

#include "ble_module.h"
#include "hw_module.h"

#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(main, LOG_LEVEL);

int main(void)
{
	hw_init();

	ble_module_init();

	LOG_INF("Bluetooth Proxy Device %d.%d.%d started!", APP_VERSION_MAJOR, APP_VERSION_MINOR,
		APP_PATCHLEVEL);

	while (1) {
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
