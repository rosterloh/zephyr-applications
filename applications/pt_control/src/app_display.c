#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_display, LOG_LEVEL_INF);

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <zephyr/app_version.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_ip.h>

#include "app_display.h"

#define FW_LINE "fw: " APP_VERSION_STRING

static const struct device *const display = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_display));
static bool initialised;
static uint8_t font_idx;

/* Latest network status, rendered once the display is up. */
static bool net_known;
static bool net_connected;
static char net_ip[NET_IPV4_ADDR_LEN];

/* Pick the 8x8 unscii font: three rows on the 128x32 panel and a full IPv4
 * address (up to 15 chars) on one line. Falls back to font 0. */
static uint8_t select_font(void)
{
	uint8_t w, h;

	for (uint8_t idx = 0; cfb_get_font_size(display, idx, &w, &h) == 0; idx++) {
		if (w == 8 && h == 8) {
			return idx;
		}
	}
	return 0;
}

/* Three 8px rows spread over the 32px panel. */
static void draw_three_lines(const char *l0, const char *l1, const char *l2)
{
	cfb_framebuffer_clear(display, false);
	cfb_framebuffer_set_font(display, font_idx);
	cfb_print(display, l0, 0, 0);
	cfb_print(display, l1, 0, 11);
	cfb_print(display, l2, 0, 22);
	cfb_framebuffer_finalize(display);
}

static void render(void)
{
	if (!initialised) {
		return;
	}

	if (net_connected && net_ip[0] != '\0') {
		draw_three_lines(FW_LINE, "connected", net_ip);
	} else if (net_connected) {
		draw_three_lines(FW_LINE, "connected", "");
	} else if (net_known) {
		draw_three_lines(FW_LINE, "WiFi down", "");
	} else {
		draw_three_lines(FW_LINE, "connecting", "");
	}
}

void app_display_set_net(bool connected, const char *ip)
{
	net_known = true;
	net_connected = connected;
	if (connected && ip != NULL) {
		strncpy(net_ip, ip, sizeof(net_ip) - 1);
		net_ip[sizeof(net_ip) - 1] = '\0';
	} else {
		net_ip[0] = '\0';
	}
	render();
}

int app_display_init(void)
{
	if (!device_is_ready(display)) {
		LOG_ERR("Display device not ready");
		return -ENODEV;
	}

	if (display_set_pixel_format(display, PIXEL_FORMAT_MONO10) != 0) {
		(void)display_set_pixel_format(display, PIXEL_FORMAT_MONO01);
	}

	if (cfb_framebuffer_init(display) != 0) {
		LOG_ERR("CFB init failed");
		return -EIO;
	}

	(void)display_blanking_off(display);
	font_idx = select_font();
	initialised = true;
	render();
	LOG_INF("Display initialised");
	return 0;
}
