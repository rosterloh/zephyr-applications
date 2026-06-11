#ifndef __APP_NETWORK_H__
#define __APP_NETWORK_H__

#include <zephyr/kernel.h>

#if IS_ENABLED(CONFIG_NETWORKING)
void app_net_connect(void);
#else
static inline void app_net_connect(void)
{
}
#endif

#endif /* __APP_NETWORK_H__ */
