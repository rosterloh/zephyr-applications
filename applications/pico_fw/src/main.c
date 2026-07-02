#include <zephyr/kernel.h>
#include <zephyr/net/net_config.h>
#include <zephyr/app_version.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

/* WiFi association is handled by conn_mgr: the in-tree AIROC driver binds
 * CONNECTIVITY_WIFI_MGMT, implemented by the wifi_connectivity module using
 * CONFIG_WIFI_CREDENTIALS_STATIC_SSID/_PASSWORD.
 */
int main(void)
{
	LOG_INF("Zephyr Application %s", APP_VERSION_STRING);

	(void)net_config_init_app(NULL, "Initialising network");

	return 0;
}
