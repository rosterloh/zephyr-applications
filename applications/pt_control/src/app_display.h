#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include <stdbool.h>

#if IS_ENABLED(CONFIG_APP_DISPLAY)
int app_display_init(void);
/* Update the network status line: shows the IP when connected, otherwise a
 * "WiFi down" notice. Safe to call before the display finishes initialising. */
void app_display_set_net(bool connected, const char *ip);
#else
static inline int app_display_init(void)
{
	return 0;
}
static inline void app_display_set_net(bool connected, const char *ip)
{
}
#endif

#endif /* APP_DISPLAY_H */
