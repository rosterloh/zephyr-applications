/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Printer state from PrusaLink's local API (Buddy firmware: MK4, Core One,
 * XL, MINI), plain HTTP with the X-Api-Key header.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/data/json.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#include "prusalink.h"

#define TIMEOUT_MS 5000

struct pl_printer {
	const char *state;
};

struct pl_status {
	struct pl_printer printer;
};

static const struct json_obj_descr printer_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct pl_printer, state, JSON_TOK_STRING),
};

static const struct json_obj_descr status_descr[] = {
	JSON_OBJ_DESCR_OBJECT(struct pl_status, printer, printer_descr),
};

/* ESP32 DRAM is nearly full; these 1.5 KB go to PSRAM where there is some. */
#ifdef CONFIG_SOC_FAMILY_ESPRESSIF_SPIRAM
#define PSRAM_BSS Z_GENERIC_SECTION(.ext_ram.bss)
#else
#define PSRAM_BSS
#endif

/* Only the uploader thread polls, so these need no lock. */
static PSRAM_BSS uint8_t recv_buf[512];
static PSRAM_BSS char body[1024]; /* a Buddy status reply is about 400 B */
static size_t body_len;

static int on_response(struct http_response *rsp, enum http_final_call final, void *user_data)
{
	if (rsp->body_found && rsp->body_frag_start != NULL) {
		if (body_len + rsp->body_frag_len > sizeof(body)) {
			body_len = SIZE_MAX; /* too big: parsed as an error */
		} else if (body_len != SIZE_MAX) {
			memcpy(&body[body_len], rsp->body_frag_start, rsp->body_frag_len);
			body_len += rsp->body_frag_len;
		}
	}
	if (final == HTTP_DATA_FINAL) {
		*(int *)user_data = rsp->http_status_code;
	}
	return 0;
}

int prusalink_state(const struct cfg_printer *p, char state[PRUSALINK_STATE_MAX])
{
	char host[NET_IPV4_ADDR_LEN];
	char key_hdr[sizeof("X-Api-Key: \r\n") + CFG_PRINTER_KEY_MAX];
	const char *headers[] = {key_hdr, NULL};
	struct net_sockaddr_in addr = {
		.sin_family = NET_AF_INET,
		.sin_port = net_htons(CONFIG_PRUSA_CAM_PRUSALINK_PORT),
		.sin_addr.s_addr = net_htonl(p->ip),
	};
	struct pl_status st = {0};
	int http_status = -EIO;
	int sock;
	int ret;

	net_addr_ntop(NET_AF_INET, &addr.sin_addr, host, sizeof(host));
	snprintf(key_hdr, sizeof(key_hdr), "X-Api-Key: %s\r\n", p->key);

	sock = zsock_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP);
	if (sock < 0) {
		return -errno;
	}
	if (zsock_connect(sock, (struct net_sockaddr *)&addr, sizeof(addr)) < 0) {
		ret = -errno;
		zsock_close(sock);
		return ret;
	}

	struct http_request req = {
		.method = HTTP_GET,
		.url = "/api/v1/status",
		.host = host,
		.protocol = "HTTP/1.1",
		.header_fields = headers,
		.response = on_response,
		.recv_buf = recv_buf,
		.recv_buf_len = sizeof(recv_buf),
	};

	body_len = 0;
	ret = http_client_req(sock, &req, TIMEOUT_MS, &http_status);
	zsock_close(sock);
	if (ret < 0) {
		return ret;
	}
	if (http_status != 200) {
		return http_status;
	}
	if (body_len == SIZE_MAX ||
	    json_obj_parse(body, body_len, status_descr, ARRAY_SIZE(status_descr), &st) < 0 ||
	    st.printer.state == NULL || strlen(st.printer.state) >= PRUSALINK_STATE_MAX) {
		return -EBADMSG;
	}
	strcpy(state, st.printer.state);
	return 0;
}

bool prusalink_printing(const char *state)
{
	return strcmp(state, "PRINTING") == 0 || strcmp(state, "PAUSED") == 0 ||
	       strcmp(state, "ATTENTION") == 0;
}
