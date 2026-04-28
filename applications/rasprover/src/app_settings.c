#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_settings, LOG_LEVEL_DBG);

#include <zephyr/init.h>
#include <zephyr/settings/settings.h>

#include "app_settings.h"

static int32_t _loop_delay_s = 60;

int32_t get_loop_delay_s(void)
{
	return _loop_delay_s;
}

static int settings_autoload(void)
{
	LOG_INF("Initialising settings subsystem");
	int err = settings_subsys_init();

	if (err) {
		LOG_ERR("Failed to initialise settings subsystem: %d", err);
		return err;
	}

	LOG_INF("Loading settings");
	return settings_load();
}

SYS_INIT(settings_autoload, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
