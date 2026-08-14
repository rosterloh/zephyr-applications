/*
 * Copyright (c) 2026 Richard Osterloh
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_CAM_MGMT_H_
#define APP_CAM_MGMT_H_

#include <zephyr/device.h>

/**
 * @brief Capture one frame into the buffer retained for the SMP camera group.
 *
 * Releases the previously retained frame, captures a new one and bumps the
 * sequence number served by the CAPTURE/READ commands.
 *
 * @return 0 on success, negative errno otherwise.
 */
int cam_mgmt_capture(const struct device *cam);

#endif /* APP_CAM_MGMT_H_ */
