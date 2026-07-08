#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_time, LOG_LEVEL_INF);

#include <limits.h>
#include <string.h>
#include <time.h>
#include <zephyr/kernel.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/sntp.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/clock.h>

#include "app_network.h"
#include "app_time.h"

#define SNTP_SERVER_PORT 123

static void sync_work_handler(struct k_work *work);
static void sntp_timeout_handler(struct k_work *work);
static void sntp_service_handler(struct net_socket_service_event *event);

K_WORK_DELAYABLE_DEFINE(sync_work, sync_work_handler);
K_WORK_DELAYABLE_DEFINE(sntp_timeout_work, sntp_timeout_handler);
NET_SOCKET_SERVICE_SYNC_DEFINE_STATIC(sntp_service, sntp_service_handler, 1);

static struct sntp_ctx sntp_ctx;
static struct net_sockaddr_storage sntp_addr;
static net_socklen_t sntp_addrlen;
static atomic_t started;
static atomic_t synced;

static uint32_t sntp_fraction_to_nsec(uint32_t fraction)
{
	return (uint32_t)(((uint64_t)fraction * NSEC_PER_SEC) >> 32);
}

static void schedule_next(bool success)
{
	k_work_reschedule(&sync_work, K_SECONDS(success ? CONFIG_APP_TIME_SNTP_RESYNC_SEC
							: CONFIG_APP_TIME_SNTP_RETRY_SEC));
}

static bool set_clock_from_sntp(const struct sntp_time *sntp_ts)
{
	struct timespec tspec;
	int ret;

	if (sntp_ts->seconds > LONG_MAX) {
		LOG_WRN("SNTP seconds value is out of range");
		return false;
	}

	tspec.tv_sec = (time_t)sntp_ts->seconds;
	tspec.tv_nsec = (long)sntp_fraction_to_nsec(sntp_ts->fraction);

	ret = sys_clock_settime(SYS_CLOCK_REALTIME, &tspec);
	if (ret < 0) {
		LOG_WRN("setting realtime clock failed: %d", ret);
		return false;
	}

	atomic_set(&synced, 1);
	LOG_INF("time synced via SNTP server %s", CONFIG_APP_TIME_SNTP_SERVER);
	return true;
}

static int start_sntp_query(struct net_sockaddr *addr, net_socklen_t addrlen)
{
	int ret;

	ret = sntp_init_async(&sntp_ctx, addr, addrlen, &sntp_service);
	if (ret < 0) {
		LOG_WRN("SNTP init failed: %d", ret);
		return ret;
	}

	ret = sntp_send_async(&sntp_ctx);
	if (ret < 0) {
		LOG_WRN("SNTP send failed: %d", ret);
		sntp_close_async(&sntp_service);
		return ret;
	}

	k_work_reschedule(&sntp_timeout_work, K_MSEC(CONFIG_APP_TIME_SNTP_TIMEOUT_MS));
	return 0;
}

static bool parse_sntp_server_addr(void)
{
	memset(&sntp_addr, 0, sizeof(sntp_addr));
	sntp_addrlen = 0;

	if (!net_ipaddr_parse(CONFIG_APP_TIME_SNTP_SERVER, sizeof(CONFIG_APP_TIME_SNTP_SERVER) - 1,
			      net_sad(&sntp_addr))) {
		return false;
	}

	if (IS_ENABLED(CONFIG_NET_IPV4) && sntp_addr.ss_family == NET_AF_INET) {
		sntp_addrlen = sizeof(struct net_sockaddr_in);
	} else if (IS_ENABLED(CONFIG_NET_IPV6) && sntp_addr.ss_family == NET_AF_INET6) {
		sntp_addrlen = sizeof(struct net_sockaddr_in6);
	} else {
		return false;
	}

	return net_port_set_default(net_sad(&sntp_addr), SNTP_SERVER_PORT) == 0;
}

static void dns_result_cb(enum dns_resolve_status status, struct dns_addrinfo *info,
			  void *user_data)
{
	ARG_UNUSED(user_data);

	if (status == DNS_EAI_CANCELED || status == DNS_EAI_FAIL) {
		LOG_WRN("DNS query for SNTP server failed");
		schedule_next(false);
		return;
	}

	if (status == DNS_EAI_ALLDONE) {
		if (sntp_addrlen == 0) {
			LOG_WRN("DNS query for SNTP server returned no IPv4 address");
			schedule_next(false);
			return;
		}

		if (start_sntp_query(net_sad(&sntp_addr), sntp_addrlen) < 0) {
			schedule_next(false);
		}
		return;
	}

	if (status != DNS_EAI_INPROGRESS || info == NULL || sntp_addrlen != 0) {
		return;
	}

	if (IS_ENABLED(CONFIG_NET_IPV4) && info->ai_family == NET_AF_INET) {
		memset(&sntp_addr, 0, sizeof(sntp_addr));
		sntp_addrlen = info->ai_addrlen;
		sntp_addr.ss_family = NET_AF_INET;
		net_ipv4_addr_copy_raw(net_sin(net_sad(&sntp_addr))->sin_addr.s4_addr,
				       net_sin(&info->ai_addr)->sin_addr.s4_addr);
		(void)net_port_set_default(net_sad(&sntp_addr), SNTP_SERVER_PORT);
	}
}

static void sync_work_handler(struct k_work *work)
{
	int ret;

	ARG_UNUSED(work);

	if (!app_net_ipv4_ready()) {
		LOG_DBG("network is not ready for SNTP");
		schedule_next(false);
		return;
	}

	if (parse_sntp_server_addr()) {
		if (start_sntp_query(net_sad(&sntp_addr), sntp_addrlen) < 0) {
			schedule_next(false);
		}
		return;
	}

	memset(&sntp_addr, 0, sizeof(sntp_addr));
	sntp_addrlen = 0;
	ret = dns_get_addr_info(CONFIG_APP_TIME_SNTP_SERVER, DNS_QUERY_TYPE_A, NULL, dns_result_cb,
				NULL, CONFIG_APP_TIME_SNTP_TIMEOUT_MS);
	if (ret < 0) {
		LOG_WRN("DNS query for SNTP server could not start: %d", ret);
		schedule_next(false);
	}
}

static void sntp_timeout_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_WRN("SNTP query timed out");
	sntp_close_async(&sntp_service);
	schedule_next(false);
}

static void sntp_service_handler(struct net_socket_service_event *event)
{
	struct sntp_time sntp_ts;
	bool success = false;
	int ret;

	ret = sntp_read_async(event, &sntp_ts);
	if (ret < 0) {
		LOG_WRN("SNTP read failed: %d", ret);
	} else {
		success = set_clock_from_sntp(&sntp_ts);
	}

	sntp_close_async(&sntp_service);
	k_work_cancel_delayable(&sntp_timeout_work);
	schedule_next(success);
}

void app_time_start(void)
{
	if (!atomic_cas(&started, 0, 1)) {
		return;
	}

	k_work_schedule(&sync_work, K_NO_WAIT);
}

bool app_time_synced(void)
{
	return atomic_get(&synced) != 0;
}

struct app_ros_time app_time_now(void)
{
	struct timespec tspec;

	if (!app_time_synced()) {
		return (struct app_ros_time){0};
	}

	if (sys_clock_gettime(SYS_CLOCK_REALTIME, &tspec) < 0 || tspec.tv_sec < 0 ||
	    tspec.tv_sec > UINT32_MAX || tspec.tv_nsec < 0 || tspec.tv_nsec >= NSEC_PER_SEC) {
		return (struct app_ros_time){0};
	}

	return (struct app_ros_time){
		.sec = (uint32_t)tspec.tv_sec,
		.nanosec = (uint32_t)tspec.tv_nsec,
	};
}
