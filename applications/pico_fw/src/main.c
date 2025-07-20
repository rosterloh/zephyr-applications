#include <zephyr/kernel.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_config.h>
#include <app_version.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

static uint8_t wifi_ssid[] = CONFIG_APP_WIFI_SSID;
static size_t wifi_ssid_len = sizeof(CONFIG_APP_WIFI_SSID) - 1;
static uint8_t wifi_psk[] = CONFIG_APP_WIFI_PSK;
static size_t wifi_psk_len = sizeof(CONFIG_APP_WIFI_PSK) - 1;

int main(void)
{
	LOG_INF("Zephyr Application %s", APP_VERSION_STRING);

	int ret;
	struct net_if *iface = net_if_get_default();

	static struct wifi_connect_req_params req_params = {
		.channel = 0,
		.security = WIFI_SECURITY_TYPE_PSK,
	};
	req_params.ssid = wifi_ssid;
	req_params.ssid_length = wifi_ssid_len;
	req_params.psk = wifi_psk;
	req_params.psk_length = wifi_psk_len;
	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &req_params,
		       sizeof(struct wifi_connect_req_params));

	(void)net_config_init_app(NULL, "Initialising network");

	return 0;
}