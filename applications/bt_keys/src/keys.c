/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Keymap: default bindings, persistence in Settings, and a shell command to
 * inspect and remap the four keys at runtime.
 */

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>

#include "keys.h"

LOG_MODULE_REGISTER(keys, LOG_LEVEL_INF);

/* Consumer-control usages (HID Usage Table, Consumer page 0x0C). */
#define USAGE_SCAN_PREV  0x00B6
#define USAGE_SCAN_NEXT  0x00B5
#define USAGE_PLAY_PAUSE 0x00CD
#define USAGE_MUTE       0x00E2

/* Default: a media pad - Prev / Play-Pause / Next / Mute. */
struct bt_keys_binding bt_keys_map[BT_KEYS_NUM_KEYS] = {
	{BT_KEYS_TYPE_CONSUMER, USAGE_SCAN_PREV},
	{BT_KEYS_TYPE_CONSUMER, USAGE_PLAY_PAUSE},
	{BT_KEYS_TYPE_CONSUMER, USAGE_SCAN_NEXT},
	{BT_KEYS_TYPE_CONSUMER, USAGE_MUTE},
};

#define SETTINGS_ROOT "btkeys"
#define SETTINGS_MAP  SETTINGS_ROOT "/map"

static int keys_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	if (settings_name_steq(name, "map", NULL)) {
		if (len != sizeof(bt_keys_map)) {
			LOG_WRN("stored keymap size mismatch (%zu), ignoring", len);
			return -EINVAL;
		}

		ssize_t rc = read_cb(cb_arg, bt_keys_map, sizeof(bt_keys_map));

		if (rc < 0) {
			return (int)rc;
		}
		LOG_INF("loaded keymap from settings");
		return 0;
	}

	return -ENOENT;
}

static struct settings_handler keys_conf = {
	.name = SETTINGS_ROOT,
	.h_set = keys_settings_set,
};

int bt_keys_settings_register(void)
{
	return settings_register(&keys_conf);
}

int bt_keys_set(uint8_t index, enum bt_keys_type type, uint16_t usage)
{
	if (index >= BT_KEYS_NUM_KEYS) {
		return -EINVAL;
	}

	bt_keys_map[index].type = (uint8_t)type;
	bt_keys_map[index].usage = usage;

	return settings_save_one(SETTINGS_MAP, bt_keys_map, sizeof(bt_keys_map));
}

/* ---------- shell ---------- */

static const char *type_str(uint8_t type)
{
	return type == BT_KEYS_TYPE_CONSUMER ? "consumer" : "keyboard";
}

static int cmd_keys_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	for (int i = 0; i < BT_KEYS_NUM_KEYS; i++) {
		shell_print(sh, "key %d: %-8s usage 0x%04x", i, type_str(bt_keys_map[i].type),
			    bt_keys_map[i].usage);
	}

	return 0;
}

static int cmd_keys_set(const struct shell *sh, size_t argc, char **argv)
{
	/* keys set <index> <keyboard|consumer> <usage-hex> */
	unsigned long index = strtoul(argv[1], NULL, 0);
	enum bt_keys_type type;

	if (!strcmp(argv[2], "keyboard") || !strcmp(argv[2], "kbd")) {
		type = BT_KEYS_TYPE_KEYBOARD;
	} else if (!strcmp(argv[2], "consumer") || !strcmp(argv[2], "cons")) {
		type = BT_KEYS_TYPE_CONSUMER;
	} else {
		shell_error(sh, "type must be 'keyboard' or 'consumer'");
		return -EINVAL;
	}

	unsigned long usage = strtoul(argv[3], NULL, 0);

	if (index >= BT_KEYS_NUM_KEYS || usage > UINT16_MAX) {
		shell_error(sh, "index 0..%d, usage 0..0xffff", BT_KEYS_NUM_KEYS - 1);
		return -EINVAL;
	}

	int err = bt_keys_set((uint8_t)index, type, (uint16_t)usage);

	if (err) {
		shell_error(sh, "failed to save: %d", err);
		return err;
	}

	shell_print(sh, "key %lu -> %s usage 0x%04lx (saved)", index, type_str(type), usage);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	keys_cmds, SHELL_CMD(list, NULL, "List current key bindings", cmd_keys_list),
	SHELL_CMD_ARG(set, NULL, "Remap a key: set <index> <keyboard|consumer> <usage-hex>",
		      cmd_keys_set, 4, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(keys, &keys_cmds, "NeoKey binding control", NULL);
