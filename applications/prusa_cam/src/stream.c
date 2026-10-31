/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * MJPEG stream (multipart/x-mixed-replace) on its own port, one viewer at a
 * time. It has its own socket and thread because the HTTP server runs every
 * dynamic response to completion on its one thread: a stream there would
 * block the config page for as long as anyone watched.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include "cam.h"
#include "cfg.h"
#include "http_util.h"

LOG_MODULE_REGISTER(stream, LOG_LEVEL_INF);

#define PORT        81 /* index.html links here */
#define IO_TIMEOUT  5  /* seconds; also bounds a viewer that stops reading */
#define AUTH_PREFIX "Authorization:"

static char req[768];

static int send_all(int sock, const void *buf, size_t len)
{
	const uint8_t *p = buf;

	while (len > 0) {
		ssize_t n = zsock_send(sock, p, len, 0);

		if (n < 0) {
			return -errno;
		}
		p += n;
		len -= n;
	}
	return 0;
}

static int send_str(int sock, const char *s)
{
	return send_all(sock, s, strlen(s));
}

/* Read the request head; 0 once "\r\n\r\n" is in req (NUL-terminated). */
static int read_head(int sock)
{
	size_t len = 0;

	while (len < sizeof(req) - 1) {
		ssize_t n = zsock_recv(sock, &req[len], sizeof(req) - 1 - len, 0);

		if (n <= 0) {
			return -EIO;
		}
		len += n;
		req[len] = '\0';
		char *end = strstr(req, "\r\n\r\n");

		if (end != NULL) {
			end[2] = '\0'; /* every header line, the last too, now ends in \r\n */
			return 0;
		}
	}
	return -E2BIG;
}

/* The Authorization header value, NUL-terminated in place, or "". */
static const char *auth_header(void)
{
	for (char *line = strstr(req, "\r\n"); line != NULL; line = strstr(line, "\r\n")) {
		line += 2;
		if (strncasecmp(line, AUTH_PREFIX, strlen(AUTH_PREFIX)) == 0) {
			char *v = line + strlen(AUTH_PREFIX);
			char *end = strstr(v, "\r\n");

			while (*v == ' ') {
				v++;
			}
			*end = '\0'; /* read_head() left a \r\n after every line */
			return v;
		}
	}
	return "";
}

/* Check method, path and credentials; on failure the error is sent. */
static bool request_ok(int sock)
{
	struct cfg c;
	bool ok;

	if (strncmp(req, "GET / ", 6) != 0 && strncmp(req, "GET /stream ", 12) != 0) {
		send_str(sock, "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
		return false;
	}
	cfg_get(&c);
	if (c.password[0] == '\0') {
		memset(&c, 0, sizeof(c));
		send_str(sock, "HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n"
			       "set a password: prusa set password\n");
		return false;
	}
	ok = http_basic_auth_ok(auth_header(), "admin", c.password);
	memset(&c, 0, sizeof(c));
	if (!ok) {
		send_str(sock, "HTTP/1.1 401 Unauthorized\r\n"
			       "WWW-Authenticate: Basic realm=\"prusa_cam\"\r\n"
			       "Connection: close\r\n\r\n");
	}
	return ok;
}

static void serve(int sock)
{
	const struct zsock_timeval tv = {.tv_sec = IO_TIMEOUT};
	char part[96];
	const uint8_t *jpeg;
	size_t len;
	int ret = 0;

	zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	zsock_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	if (read_head(sock) < 0 || !request_ok(sock)) {
		return;
	}
	memset(req, 0, sizeof(req)); /* drop the credentials */

	if (send_str(sock, "HTTP/1.1 200 OK\r\n"
			   "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
			   "Cache-Control: no-store\r\nConnection: close\r\n\r\n") < 0) {
		return;
	}
	LOG_INF("Viewer connected");

	while (ret == 0) {
		if (cam_frame(&jpeg, &len) < 0) {
			k_sleep(K_SECONDS(1));
			continue;
		}
		snprintf(part, sizeof(part),
			 "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
			 (unsigned int)len);
		ret = send_str(sock, part);
		if (ret == 0) {
			ret = send_all(sock, jpeg, len);
		}
		cam_release();
		if (ret == 0) {
			ret = send_str(sock, "\r\n");
		}
	}
	LOG_INF("Viewer gone (%d)", ret);
}

static void stream_thread(void *a, void *b, void *c)
{
	struct net_sockaddr_in addr = {
		.sin_family = NET_AF_INET,
		.sin_port = net_htons(PORT),
	};
	int one = 1;
	int srv = zsock_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP);

	if (srv < 0 || zsock_setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0 ||
	    zsock_bind(srv, (struct net_sockaddr *)&addr, sizeof(addr)) < 0 ||
	    zsock_listen(srv, 1) < 0) {
		LOG_ERR("Stream port %d unavailable (%d)", PORT, -errno);
		return;
	}

	for (;;) {
		int sock = zsock_accept(srv, NULL, NULL);

		if (sock < 0) {
			k_sleep(K_SECONDS(1));
			continue;
		}
		serve(sock);
		zsock_close(sock);
	}
}

K_THREAD_DEFINE(stream_tid, CONFIG_PRUSA_CAM_STREAM_STACK_SIZE, stream_thread, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
