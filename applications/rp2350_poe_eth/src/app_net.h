/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_NET_H_
#define APP_NET_H_

#include <zephyr/kernel.h>

/**
 * @brief Bring the W6300 interface up and start DHCPv4.
 *
 * @retval 0 on success.
 * @retval -ENODEV if no Ethernet interface was found.
 */
int app_net_init(void);

/**
 * @brief Block until DHCPv4 has leased an IPv4 address.
 *
 * @param timeout How long to wait.
 *
 * @retval 0 once an address is bound.
 * @retval -ETIMEDOUT if the lease was not obtained in time.
 */
int app_net_wait_for_address(k_timeout_t timeout);

#endif /* APP_NET_H_ */
