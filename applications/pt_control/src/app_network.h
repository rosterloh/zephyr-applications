#ifndef __APP_NETWORK_H__
#define __APP_NETWORK_H__

#include <zephyr/kernel.h>

#if IS_ENABLED(CONFIG_NETWORKING)
void app_net_connect(void);
bool app_net_ipv4_ready(void);
#else
static inline void app_net_connect(void)
{
}

static inline bool app_net_ipv4_ready(void)
{
	return false;
}
#endif

#endif /* __APP_NETWORK_H__ */
