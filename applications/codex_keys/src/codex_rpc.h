/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CODEX_RPC_H_
#define CODEX_RPC_H_

#include <stdint.h>

/* Key actions understood by the host. */
#define CODEX_ACT_RELEASE 0
#define CODEX_ACT_PRESS   1
#define CODEX_ACT_STEP    2

/*
 * Emit a key event. `key` is a host key id ("AG00".."AG05" for agent keys,
 * "ACT06".. for command keys); `agent` is the agent slot for agent keys, or -1.
 * Blocks briefly while fragments are notified, so call from a thread.
 */
void codex_rpc_send_key(const char *key, uint8_t action, int agent);

#endif /* CODEX_RPC_H_ */
