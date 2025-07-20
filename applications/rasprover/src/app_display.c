#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_display, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>

#include <app_version.h>

static const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static bool initialised = false;

static lv_obj_t *screen;

void display_tick_cb(struct k_work *work)
{
	lv_task_handler();
}

#define TICK_MS 10

K_WORK_DEFINE(display_tick_work, display_tick_cb);

#if IS_ENABLED(CONFIG_APP_DISPLAY_WORK_QUEUE_DEDICATED)

K_THREAD_STACK_DEFINE(display_work_stack_area, CONFIG_APP_DISPLAY_DEDICATED_THREAD_STACK_SIZE);

static struct k_work_q display_work_q;

#endif

struct k_work_q *app_display_work_q()
{
#if IS_ENABLED(CONFIG_APP_DISPLAY_WORK_QUEUE_DEDICATED)
	return &display_work_q;
#else
	return &k_sys_work_q;
#endif
}

void display_timer_cb()
{
	k_work_submit_to_queue(app_display_work_q(), &display_tick_work);
}

K_TIMER_DEFINE(display_timer, display_timer_cb, NULL);

void unblank_display_cb(struct k_work *work)
{
	display_blanking_off(display);
	k_timer_start(&display_timer, K_MSEC(TICK_MS), K_MSEC(TICK_MS));
}

#if IS_ENABLED(CONFIG_DISPLAY_BLANK_ON_IDLE)

void blank_display_cb(struct k_work *work)
{
	k_timer_stop(&display_timer);
	display_blanking_on(display);
}
K_WORK_DEFINE(blank_display_work, blank_display_cb);
K_WORK_DEFINE(unblank_display_work, unblank_display_cb);

static void start_display_updates()
{
	if (display == NULL) {
		return;
	}

	k_work_submit_to_queue(zmk_display_work_q(), &unblank_display_work);
}

static void stop_display_updates()
{
	if (display == NULL) {
		return;
	}

	k_work_submit_to_queue(zmk_display_work_q(), &blank_display_work);
}

#endif

lv_obj_t *app_display_status_screen()
{
	lv_obj_t *screen;
	screen = lv_obj_create(NULL);

	lv_obj_t *version_label;
	char text[17] = {};
	snprintf(text, sizeof(text), "Rasprover v%s", APP_VERSION_STRING);
	version_label = lv_label_create(screen); // lv_screen_active()
	lv_label_set_text(version_label, text);
	lv_obj_align(version_label, LV_ALIGN_CENTER, 0, 0);

	return screen;
}

void initialise_display(struct k_work *work)
{
	if (!device_is_ready(display)) {
		LOG_ERR("Failed to find display device");
		return;
	}

	initialised = true;

	screen = app_display_status_screen();

	if (screen == NULL) {
		LOG_ERR("No status screen provided");
		return;
	}

	lv_scr_load(screen);

	unblank_display_cb(work);
}

K_WORK_DEFINE(init_work, initialise_display);

int app_display_init(void)
{
#if IS_ENABLED(CONFIG_APP_DISPLAY_WORK_QUEUE_DEDICATED)
	k_work_queue_start(&display_work_q, display_work_stack_area,
			   K_THREAD_STACK_SIZEOF(display_work_stack_area),
			   CONFIG_APP_DISPLAY_DEDICATED_THREAD_PRIORITY, NULL);
#endif

	k_work_submit_to_queue(app_display_work_q(), &init_work);

	return 0;
}
