/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_build_id, LOG_LEVEL_INF);

#include <stdio.h>
#include <string.h>
#include <zephyr/init.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_APP_BUILD_ID_COREDUMP
#include <zephyr/drivers/coredump.h>
#endif
#ifdef CONFIG_APP_BUILD_ID_MCUMGR
#include <zcbor_common.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt.h>
#endif

#include "app_build_id.h"

#define NT_GNU_BUILD_ID 3
#define BUILD_ID_MAX    20 /* SHA-1 */
/* Short form for the boot log: 6 bytes, as git and memfault abbreviate. */
#define BUILD_ID_SHORT  12

struct gnu_note {
	uint32_t namesz;
	uint32_t descsz;
	uint32_t type;
	char name[4];
	uint8_t desc[];
};

/* Placed by build_id.ld at the start of the linker's .note.gnu.build-id. */
extern const struct gnu_note app_build_id_note;

static char id_hex[BUILD_ID_MAX * 2 + 1];

size_t app_build_id_get(const uint8_t **id)
{
	const struct gnu_note *n = &app_build_id_note;

	if (n->type != NT_GNU_BUILD_ID || n->namesz != 4 || memcmp(n->name, "GNU", 4) != 0 ||
	    n->descsz == 0 || n->descsz > BUILD_ID_MAX) {
		*id = NULL;
		return 0;
	}
	*id = n->desc;
	return n->descsz;
}

const char *app_build_id_str(void)
{
	return id_hex;
}

#ifdef CONFIG_APP_BUILD_ID_COREDUMP
/* A RAM copy, so the dump never reads through the flash cache while the flash
 * backend may be writing the partition. 36 bytes. */
static uint8_t note_copy[sizeof(struct gnu_note) + BUILD_ID_MAX] __aligned(4);
static struct coredump_mem_region_node note_region;

static void register_coredump(size_t len)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(app_coredump));

	note_region.start = (uintptr_t)note_copy;
	note_region.size = sizeof(struct gnu_note) + len;
	memcpy(note_copy, &app_build_id_note, note_region.size);

	if (!device_is_ready(dev) || !coredump_device_register_memory(dev, &note_region)) {
		LOG_ERR("Build ID not registered with the coredump");
	}
}
#endif

#ifdef CONFIG_APP_BUILD_ID_MCUMGR
#define OS_INFO_BUILD_ID OS_MGMT_INFO_FORMAT_USER_CUSTOM_START

static enum mgmt_cb_return os_info_cb(uint32_t event, enum mgmt_cb_return prev_status, int32_t *rc,
				      uint16_t *group, bool *abort_more, void *data,
				      size_t data_size)
{
	if (event == MGMT_EVT_OP_OS_MGMT_INFO_CHECK) {
		struct os_mgmt_info_check *check = data;

		for (size_t i = 0; i < check->format->len; i++) {
			if (check->format->value[i] == 'g') {
				*check->format_bitmask |= OS_INFO_BUILD_ID;
				++(*check->valid_formats);
			}
		}
	} else if (event == MGMT_EVT_OP_OS_MGMT_INFO_APPEND) {
		struct os_mgmt_info_append *append = data;
		size_t room = append->buffer_size - *append->output_length;
		int n;

		if (!append->all_format_specified &&
		    !(*append->format_bitmask & OS_INFO_BUILD_ID)) {
			return MGMT_CB_OK;
		}
		n = snprintf((char *)&append->output[*append->output_length], room, "%s%s",
			     *append->prior_output ? " " : "", id_hex);
		if (n < 0 || n >= room) {
			*abort_more = true;
			*rc = -1;
			return MGMT_CB_ERROR_RC;
		}
		*append->output_length += (uint16_t)n;
		*append->prior_output = true;
		*append->format_bitmask &= ~OS_INFO_BUILD_ID;
	}

	return MGMT_CB_OK;
}

static struct mgmt_callback os_info_callback = {
	.callback = os_info_cb,
	.event_id = MGMT_EVT_OP_OS_MGMT_INFO_CHECK | MGMT_EVT_OP_OS_MGMT_INFO_APPEND,
};
#endif

static int app_build_id_boot(void)
{
	const uint8_t *id;
	size_t len = app_build_id_get(&id);

	if (len == 0) {
		LOG_WRN("Image carries no GNU build ID");
		return 0;
	}
	bin2hex(id, len, id_hex, sizeof(id_hex));
	LOG_INF("Build ID %.*s", BUILD_ID_SHORT, id_hex);

#ifdef CONFIG_APP_BUILD_ID_COREDUMP
	register_coredump(len);
#endif
#ifdef CONFIG_APP_BUILD_ID_MCUMGR
	mgmt_callback_register(&os_info_callback);
#endif

	return 0;
}

SYS_INIT(app_build_id_boot, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
