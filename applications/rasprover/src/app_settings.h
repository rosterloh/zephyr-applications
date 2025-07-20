#ifndef __APP_SETTINGS_H__
#define __APP_SETTINGS_H__

#include <stdint.h>
#include <golioth/client.h>

int32_t get_loop_delay_s(void);
int app_settings_register(struct golioth_client *client);

const struct golioth_client_config *golioth_app_credentials_get(void);

#endif /* __APP_SETTINGS_H__ */