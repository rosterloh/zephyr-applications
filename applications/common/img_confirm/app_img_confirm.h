/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Confirm a new MCUboot image only once the application has shown it works,
 * rather than on boot. An image that boots but never gets there is reset by
 * a task-watchdog deadline, and MCUboot reverts it.
 */

#ifndef APP_IMG_CONFIRM_H
#define APP_IMG_CONFIRM_H

#ifdef CONFIG_APP_IMG_CONFIRM

/* Call once, after app_watchdog_init(). If the running image is on trial,
 * registers an "img_confirm" watchdog channel that only app_img_confirm()
 * deletes. */
void app_img_confirm_arm(void);

/* Call wherever the application is healthy; cheap and thread-safe, so it may
 * be called on every success. Confirms the image on the first call. */
void app_img_confirm(void);

#else

static inline void app_img_confirm_arm(void)
{
}

static inline void app_img_confirm(void)
{
}

#endif /* CONFIG_APP_IMG_CONFIRM */

#endif /* APP_IMG_CONFIRM_H */
