#ifndef APP_HTTP_H
#define APP_HTTP_H

#if IS_ENABLED(CONFIG_APP_HTTP)
int app_http_start(void);
#else
static inline int app_http_start(void)
{
	return 0;
}
#endif

#endif /* APP_HTTP_H */
