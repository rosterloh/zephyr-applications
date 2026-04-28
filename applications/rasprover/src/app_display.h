#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

int  app_display_init(void);
void app_display_update_power(double v, double i, double p);

#if IS_ENABLED(CONFIG_APP_DISPLAY_BLANK_ON_IDLE)
void app_display_blank(void);
void app_display_unblank(void);
#endif

#endif /* APP_DISPLAY_H */
