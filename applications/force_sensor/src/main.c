#include <zephyr/kernel.h>
#include <app_version.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

int main(void)
{
	LOG_INF("Force Sensor %s", APP_VERSION_STRING);

	return 0;
}