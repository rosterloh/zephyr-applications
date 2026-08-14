#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include "app_display.h"
#include "app_gimbal.h"
#include "app_motors.h"
#include "app_network.h"
#include "app_sensors.h"
#include "app_settings.h"
#include "app_time.h"
#include "app_watchdog.h"
#include "app_zenoh.h"
#include <zephyr/kernel.h>

static k_tid_t _system_thread = 0;

void wake_system_thread(void)
{
	k_wakeup(_system_thread);
}

int main(void)
{
	LOG_INF("Firmware version: %s", STRINGIFY(APP_VERSION_MAJOR) "." STRINGIFY(APP_VERSION_MINOR) "." STRINGIFY(APP_PATCHLEVEL));

	_system_thread = k_current_get();

	/* Cover the init sequence, not just the superloop: the hangs this
	 * watchdog exists for happen during boot, inside app_net_connect().
	 *
	 * Sized off the longest legitimate *single* step rather than the total,
	 * because the channel is fed between steps. That step is
	 * app_net_connect(), bounded at NET_CONNECT_TIMEOUT (30 s) by its
	 * k_sem_take(). Everything else is either non-blocking (app_time_start()
	 * and app_display_init() only submit work) or bounded well under it
	 * (z_open() by APP_ZENOH_TRANSPORT_CONNECT_TIMEOUT_MS, 10 s; the locator
	 * is an IP literal, so no DNS resolve). 60 s is 2x that worst step,
	 * leaving room for a driver that overruns its own bound. */
	app_watchdog_init();
	int wdt_channel = app_watchdog_register("boot", 60000);

	app_sensors_init();
#ifdef CONFIG_APP_MOTORS
	app_motors_init();
#endif
#ifdef CONFIG_APP_GIMBAL
	app_gimbal_init();
#endif

	app_watchdog_feed(wdt_channel);
	app_net_connect();
	if (app_net_ipv4_ready()) {
		app_watchdog_feed(wdt_channel);
		app_time_start();
		app_zenoh_init();
	} else {
		LOG_WRN("Network unavailable; skipping time sync and zenoh");
	}

	app_watchdog_feed(wdt_channel);
#ifdef CONFIG_APP_DISPLAY
	app_display_init();
#endif

	/* Boot done: swap the boot channel for the steady-state one. Its timeout
	 * is derived from the loop delay rather than hardcoded, so it can't
	 * silently break when that setting changes. One registration is enough
	 * because the only writer of the delay is settings_load(), which runs
	 * from SYS_INIT at APPLICATION level — i.e. before main() — so it cannot
	 * change while the loop runs.
	 * ponytail: single registration; if a runtime setter for the loop delay
	 * is ever added, re-register (unregister + register) when it changes.
	 *
	 * 3x the loop period plus 10 s of headroom tolerates a slow sensor read
	 * or a blocked zenoh publish while still catching a real stall. */
	app_watchdog_unregister(wdt_channel);
	wdt_channel = app_watchdog_register("main", get_loop_delay_s() * 3000 + 10000);

	while (true) {
		app_watchdog_feed(wdt_channel);
		app_sensors_read_and_stream();

		k_sleep(K_SECONDS(get_loop_delay_s()));
	}
}
