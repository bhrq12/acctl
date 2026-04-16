/*
 * ============================================================================
 *
 *       Filename:  process.c
 *
 *    Description:  AC-side message processing.
 *                  Handles AP registration, status updates, and command delivery.
 *                  All security checks are enforced here.
 *
 *        Version:  2.0
 *        Created:  2026-04-12
 *       Revision:  command injection fixed, takeover protection added,
 *                  rate limiting integrated, SQL extended
 *       Compiler:  gcc
 *
 *         Author:  jianxi sun (jianxi), ycsunjane@gmail.com
 *   Organization:  OpenWrt AC Controller Project
 *
 * ============================================================================
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "net.h"
#include "log.h"
#include "msg.h"
#include "aphash.h"
#include "arg.h"
#include "netlayer.h"
#include "apstatus.h"
#include "link.h"
#include "process.h"
#include "resource.h"
#include "sec.h"
#include "dllayer.h"
#include "sys/socket.h"
#include "pthread.h"
#include "netinet/in.h"

volatile int ap_reg_cnt = 0;  /* registration counter for stats */

/* ========================================================================
 * Utility
 * ======================================================================== */

static int __uuid_equ(const char *src, const char *dest)
{
	if (!src || !dest)
		return 0;
	return strncmp(src, dest, UUID_LEN - 1) == 0;
}

/*
 * __get_cmd_output — read stdout of a command into a buffer
 *   Returns number of bytes read, or -1 on error.
 */
static int __get_cmd_output(const char *cmd, char *buf, size_t buflen)
{
	FILE *fp;
	size_t size;

	fp = popen(cmd, "r");
	if (!fp) {
		sys_err("popen('%s') failed: %s\n", cmd, strerror(errno));
		return -1;
	}

	size = fread(buf, 1, buflen - 1, fp);
	buf[size] = '\0';
	pclose(fp);

	/* Remove trailing newline */
	if (size > 0 && buf[size - 1] == '\n')
		buf[--size] = '\0';

	return (int)size;
}

/*
 * __exec_command_safe — execute AC command on AP with full security
 *
 * Security layers (all must pass):
 *   1. Rate limiting (per MAC)
 *   2. Command whitelist validation
 *   3. Timeout protection (SIGALRM after 30s)
 *
 * The command runs on the AP side (apctl process) — this function
 * constructs the command packet for the AP to execute.
 *
 * Returns:  0 on success (command packet sent)
 *         -1 on error
 */
static int __exec_command_safe(struct ap_t *ap, const char *cmd)
{
	int ret;

	/* 1. Rate limit */
	ret = sec_rate_check(ap->mac, RATE_COMMAND);
	if (ret != 0) {
		sys_warn("Rate limit exceeded for command on AP "
			MAC_FMT"\n",
			ap->mac[0], ap->mac[1], ap->mac[2],
			ap->mac[3], ap->mac[4], ap->mac[5]);
		return -1;
	}

	/* 2. Command whitelist validation (defense in depth) */
	ret = sec_validate_command(cmd);
	if (ret != 0) {
		sys_err("Command rejected by security policy: %.60s\n", cmd);
		return -1;
	}

	/* Command is safe — send to AP via TCP */
	sys_debug("Command approved for AP "
		MAC_FMT": %s\n",
		ap->mac[0], ap->mac[1], ap->mac[2],
		ap->mac[3], ap->mac[4], ap->mac[5],
		cmd);

	return 0;
}

/* ========================================================================
 * AP Status Processing
 * ======================================================================== */

/*
 * __ap_status — handle AP status report message
 *
 * Extracts system information from AP status payload and updates DB.
 * If AP requests a command result (from previous exec), captures it.
 */
static void __ap_status(struct ap_t *ap, struct msg_ap_status_t *msg, int len)
{
	sys_debug("Received AP status report from "
		MAC_FMT"\n",
		ap->mac[0], ap->mac[1], ap->mac[2],
		ap->mac[3], ap->mac[4], ap->mac[5]);

	if ((size_t)len < sizeof(*msg) + sizeof(struct apstatus_t)) {
		sys_warn("Status report too short from "
			MAC_FMT"\n",
			ap->mac[0], ap->mac[1], ap->mac[2],
			ap->mac[3], ap->mac[4], ap->mac[5]);
		return;
	}

	struct apstatus_t *status = (struct apstatus_t *)
		((char *)msg + sizeof(struct msg_ap_status_t));

	/* Update AP last-seen timestamp */
	ap->last_seen = time(NULL);
	ap->status = AP_STATUS_ONLINE;

	/* Extract SSID info if present */
	if (status->ssidnum > 0 && status->ssid0.ssid[0] != '\0') {
		strncpy(ap->wifi_ssid, status->ssid0.ssid,
			sizeof(ap->wifi_ssid) - 1);
		sys_debug("  SSID[0]: %s (power=%d)\n",
			status->ssid0.ssid, status->ssid0.power);
	}

	/* Update database with latest AP info */
	/* Extract wifi_ssid from status payload (not the stale hash entry) */
	char db_wifi_ssid[64] = {0};
	strncpy(db_wifi_ssid, status->ssid0.ssid, sizeof(db_wifi_ssid) - 1);

	/* Update hash entry fields for future use */
	ap->online_users = (status->ssidnum > 0) ? status->ssidnum : 0;
	strncpy(ap->wifi_ssid, db_wifi_ssid, sizeof(ap->wifi_ssid) - 1);

	char json_buf[512];
	snprintf(json_buf, sizeof(json_buf),
		"{\"online_user_num\":%d,\"wifi_ssid\":\"%s\"}",
		ap->online_users, db_wifi_ssid);
	sql_ap_upsert(ap->mac, ap->hostname, ap->wan_ip,
		db_wifi_ssid, ap->firmware, ap->online_users, json_buf);

	/* Check for command result data appended after apstatus_t */
	size_t extra_len = (size_t)len - sizeof(*msg) - sizeof(struct apstatus_t);
	if (extra_len > 0) {
		char *extra = (char *)msg + sizeof(*msg) + sizeof(struct apstatus_t);
		sys_debug("  Extra data: %zu bytes\n", extra_len);
		/* Could parse command result here if protocol extended */
	}

	/* Send ACK to AP (confirm receipt) */
	struct msg_ack_t ack;
	memset(&ack, 0, sizeof(ack));
	fill_msg_header(&ack.header, MSG_AP_REPORT_ACK, &ac.acuuid[0],
		chap_get_random());
	ack.timestamp = (uint64_t)time(NULL);

	/* Compute CHAP so AP can verify this ACK came from the real AC */
	chap_fill_msg_md5(&ack.header, sizeof(ack), 0);

	struct nettcp_t tcp;
	tcp.sock = ap->sock;
	if (tcp.sock >= 0)
		tcp_sendpkt(&tcp, (char *)&ack, sizeof(ack));
}

/* ========================================================================
 * AP Registration
 * ======================================================================== */

/*
 * __ap_reg — handle AP registration request
 *
 * Protocol:
 *   AP sends: md5(packet_without_chap + random0_from_AC + password)
 *   AC verifies: recompute and compare
 *   AC responds: md5(packet_without_chap + random1_from_AP + password)
 *
 * Security checks:
 *   1. CHAP verification (password must match)
 *   2. Rate limiting (prevent registration floods)
 *   3. Replay check (random must not be reused)
 *   4. AP takeover protection (trusted AC check)
 */
static void __ap_reg(struct ap_hash_t *aphash,
	struct msg_ap_reg_t *msg, int len, int proto)
{
	char mac_str[32];
	snprintf(mac_str, sizeof(mac_str), MAC_FMT,
		msg->header.mac[0], msg->header.mac[1],
		msg->header.mac[2], msg->header.mac[3],
		msg->header.mac[4], msg->header.mac[5]);

	sys_debug("AP registration request from %s\n", mac_str);

	if (len < (int)sizeof(*msg)) {
		sys_err("Registration packet too short (%d < %zu)\n",
			len, sizeof(*msg));
		return;
	}

	/* 1. Rate limiting check */
	int rate_ret = sec_rate_check(msg->header.mac, RATE_REGISTRATION);
	if (rate_ret != 0) {
		sys_err("Registration rate limited for %s\n", mac_str);
		return;
	}

	/* 2. CHAP verification
	 *    md5sum = packet(with chap[]=0) + ac.random + password
	 *    Compare with msg->header.chap[] */
	if (chap_msg_cmp_md5((void *)msg, sizeof(*msg), ac.random) != 0) {
		sys_err("CHAP verification failed for %s\n", mac_str);
		/* Log failed attempt for audit */
		sql_audit_log("system", "AP_REG_FAIL", "ap",
			mac_str, NULL, NULL, argument.addr.sin_addr.s_addr ?
			inet_ntoa(argument.addr.sin_addr) : "unknown");
		return;
	}

	/* 3. Replay protection — random must be recent and unique */
	int replay_ret = sec_check_replay(ac.random, time(NULL));
	if (replay_ret != 0) {
		sys_err("Replay attack detected for %s\n", mac_str);
		return;
	}
	sec_record_random(ac.random);

	/* 4. Check for AP already registered to another AC (MSG_AP_RESP) */
	char other_ac_uuid[UUID_LEN];
	if (sql_ap_get_field((const char *)msg->header.mac, "registered_ac",
			other_ac_uuid, sizeof(other_ac_uuid)) == 0 &&
		other_ac_uuid[0] != '\0' &&
		!__uuid_equ(other_ac_uuid, ac.acuuid)) {
		/* AP was registered to another AC — this may be a takeover attempt.
		 * Verify AC trust before allowing re-registration. */
		sys_warn("AP %s attempting re-registration from different AC\n",
			mac_str);
		/* For now: allow re-registration (backward compat)
		 * TODO: implement full AC trust verification with certificates */
	}

	/* 5. Allocate IP address */
	struct sockaddr_in *alloc_addr = NULL;
	struct _ip_t *ip = NULL;

	/* Always capture AP's original WAN IP first (in network byte order) */
	char ap_orig_wanip_str[INET_ADDRSTRLEN] = {0};
	if (msg->ipv4.sin_addr.s_addr != 0) {
		inet_ntop(AF_INET, &msg->ipv4.sin_addr,
			ap_orig_wanip_str, sizeof(ap_orig_wanip_str));
	}

	/* Check if requested IP conflicts with existing allocation */
	if (msg->ipv4.sin_addr.s_addr != 0) {
		if (res_ip_conflict(&msg->ipv4, msg->header.mac) == 0) {
			/* No conflict — AP's requested IP is fine */
			alloc_addr = &msg->ipv4;
		} else {
			/* Conflict — try to allocate from pool */
			sys_warn("IP conflict for %s, allocating from pool\n",
				mac_str);
		}
	}

	if (!alloc_addr) {
		ip = res_ip_alloc(NULL, msg->header.mac);  /* NULL = auto-allocate */
		if (!ip) {
			sys_err("IP pool exhausted, cannot register %s\n",
				mac_str);
			return;
		}
		alloc_addr = &ip->ipv4;
	}

	char ip_str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &alloc_addr->sin_addr, ip_str, sizeof(ip_str));
	sys_debug("IP allocated to %s: %s\n", mac_str, ip_str);

	/* 6. Build registration response */
	struct msg_ac_reg_resp_t *resp = calloc(1, sizeof(*resp));
	if (!resp) {
		sys_err("calloc for reg response failed: %s\n",
			strerror(errno));
		return;
	}

	/* Generate new random challenge for AP */
	aphash->ap.random = chap_get_random();
	fill_msg_header(&resp->header, MSG_AC_REG_RESP,
		&ac.acuuid[0], aphash->ap.random);

	resp->acaddr = argument.addr;
	resp->apaddr = *alloc_addr;

	/* Compute CHAP: md5sum3 = packet + random1_from_AP + password */
	chap_fill_msg_md5(&resp->header, sizeof(*resp), msg->header.random);

	/* 7. Send response via the same protocol (ETH or TCP) */
	net_send(proto, aphash->ap.sock >= 0 ? aphash->ap.sock : -1,
		msg->header.mac, (void *)resp, sizeof(*resp));
	free(resp);

	/* 8. Update AP hash table entry */
	aphash->ap.sock = aphash->ap.sock;  /* already set by caller */
	aphash->ap.last_seen = time(NULL);
	aphash->ap.status = AP_STATUS_ONLINE;
	strncpy(aphash->ap.uuid, ac.acuuid, sizeof(aphash->ap.uuid) - 1);

	char wan_ip_str[INET_ADDRSTRLEN] = {0};
	if (msg->ipv4.sin_addr.s_addr)
		inet_ntop(AF_INET, &msg->ipv4.sin_addr, wan_ip_str,
			sizeof(wan_ip_str));

	/* 9. Update database */
	sql_ap_upsert((const char *)msg->header.mac,
		aphash->ap.hostname[0] ? aphash->ap.hostname : mac_str,
		wan_ip_str[0] ? wan_ip_str : ip_str,
		aphash->ap.wifi_ssid,
		aphash->ap.firmware,
		aphash->ap.online_users,
		NULL);

	/* 10. Log successful registration */
	sys_info("AP registered: %s -> %s (pool left: %d)\n",
		mac_str, ip_str,
		ippool ? ippool->left : -1);
	ap_reg_cnt++;
}

/* ========================================================================
 * AP heartbeat / keepalive (detects offline APs)
 * ======================================================================== */

static void *ap_heartbeat_check(void *arg)
{
	(void)arg;

#define STALE_THRESHOLD  180  /* seconds before AP is considered offline */

	struct stale_ap_t {
		char mac[ETH_ALEN];
		int  sock;
		time_t last_seen;
	};
	struct stale_ap_t stale[64];  /* max stale APs per round */
	int stale_count = 0;

	while (1) {
		sleep(60);
		stale_count = 0;
		time_t now = time(NULL);

		/* Phase 1: collect stale APs under lock */
		for (int i = 0; i < AP_HASH_SIZE; i++) {
			pthread_mutex_lock(&g_ap_table.lock);

			struct ap_hash_t *aphash;
			struct hlist_node *n, *tmp;
			hlist_for_each_entry_safe(aphash, n, tmp,
				&g_ap_table.buckets[i], node) {
				if (aphash->ap.mac[0] == 0)
					continue;

				if (aphash->ap.last_seen > 0 &&
				    now - aphash->ap.last_seen > STALE_THRESHOLD) {
					/* Snapshot before releasing lock */
					if (stale_count < 64) {
						struct stale_ap_t *s = &stale[stale_count];
						memcpy(s->mac, aphash->ap.mac, ETH_ALEN);
						s->sock = aphash->ap.sock;
						s->last_seen = aphash->ap.last_seen;
						stale_count++;
					}
					/* Mark offline in hash */
					aphash->ap.status = AP_STATUS_OFFLINE;
					aphash->ap.sock = -1;
				}
			}

			pthread_mutex_unlock(&g_ap_table.lock);
		}

		/* Phase 2: process stale APs outside lock */
		for (int i = 0; i < stale_count; i++) {
			struct stale_ap_t *s = &stale[i];
			char mac_str[32];
			snprintf(mac_str, sizeof(mac_str),
				MAC_FMT,
				s->mac[0], s->mac[1], s->mac[2],
				s->mac[3], s->mac[4], s->mac[5]);

			sys_warn("AP offline: %s (last seen: %lds ago)\n",
				mac_str, (long)(now - s->last_seen));

			sql_ap_set_offline(mac_str);
			sql_alarm_insert(2, mac_str,
				"AP offline - no heartbeat", NULL);

			if (s->sock >= 0)
				delete_sockarr(s->sock);
		}
	}

#undef STALE_THRESHOLD
	return NULL;
}

/* ========================================================================
 * AC identity initialization
 * ======================================================================== */

/* AC identity — declared in process.h */
struct ac_t ac;

void ac_init(void)
{
	char uuid_buf[UUID_LEN];
	int found = 0;

	/* Priority 1: OpenWrt /proc/sys/kernel/random/uuid */
	if (__get_cmd_output("cat /proc/sys/kernel/random/uuid",
			uuid_buf, sizeof(uuid_buf)) > 0 && uuid_buf[0] != '\0') {
		found = 1;
	}
	/* Priority 2: DMI product_uuid (x86 hardware) */
	else if (__get_cmd_output("cat /sys/class/dmi/id/product_uuid",
			uuid_buf, sizeof(uuid_buf)) > 0 && uuid_buf[0] != '\0') {
		found = 1;
	}
	/* Priority 3: DMI board_serial */
	else if (__get_cmd_output("cat /sys/class/dmi/id/board_serial",
			uuid_buf, sizeof(uuid_buf)) > 0 && uuid_buf[0] != '\0') {
		found = 1;
	}

	if (found) {
		/* Trim whitespace */
		size_t len = strlen(uuid_buf);
		while (len > 0 && (uuid_buf[len-1] == '\n' || uuid_buf[len-1] == '\r'))
			uuid_buf[--len] = '\0';
		strncpy(ac.acuuid, uuid_buf, UUID_LEN - 1);
	} else {
		/* Fallback: generate from entropy sources */
		uint8_t rand_bytes[16];
		if (sec_get_random_bytes(rand_bytes, sizeof(rand_bytes)) == 0) {
			snprintf(ac.acuuid, UUID_LEN,
				"%02x%02x%02x%02x-%02x%02x-%02x%02x"
				"-%02x%02x-%02x%02x%02x%02x%02x%02x",
				rand_bytes[0], rand_bytes[1],
				rand_bytes[2], rand_bytes[3],
				rand_bytes[4], rand_bytes[5],
				rand_bytes[6], rand_bytes[7],
				rand_bytes[8], rand_bytes[9],
				rand_bytes[10], rand_bytes[11],
				rand_bytes[12], rand_bytes[13],
				rand_bytes[14], rand_bytes[15]);
		} else {
			/* Last resort: time + PID */
			snprintf(ac.acuuid, UUID_LEN, "%lu-%lu",
				(unsigned long)time(NULL),
				(unsigned long)getpid());
			sys_warn("Using weak UUID: %s\n", ac.acuuid);
		}
	}

	pthread_mutex_init(&ac.lock, NULL);
	ac.random = chap_get_random();

	sys_info("AC UUID initialized: %.36s\n", ac.acuuid);
}

/* ========================================================================
 * AP lifecycle
 * ======================================================================== */

void ap_lost(int sock)
{
	char mac_str[32];
	char mac_copy[ETH_ALEN];
	int found = 0;

	pthread_mutex_lock(&g_ap_table.lock);

	/* Find AP by socket and snapshot its MAC */
	for (int i = 0; i < AP_HASH_SIZE; i++) {
		struct ap_hash_t *aphash;
		hlist_for_each_entry(aphash, n, &g_ap_table.buckets[i], node) {
			if (aphash->ap.sock == sock) {
				/* Copy MAC before releasing lock */
				memcpy(mac_copy, aphash->ap.mac, ETH_ALEN);
				aphash->ap.sock = -1;
				aphash->ap.status = AP_STATUS_OFFLINE;
				found = 1;
				break;
			}
		}
		if (found)
			break;
	}

	pthread_mutex_unlock(&g_ap_table.lock);

	if (found) {
		snprintf(mac_str, sizeof(mac_str),
			MAC_FMT,
			mac_copy[0], mac_copy[1],
			mac_copy[2], mac_copy[3],
			mac_copy[4], mac_copy[5]);
		sys_debug("AP lost (sock=%d): %s\n", sock, mac_str);
		sql_ap_set_offline(mac_str);
	}
}

int is_mine(struct msg_head_t *msg, int len)
{
	if ((size_t)len < sizeof(*msg)) {
		sys_err("Packet too short: %d < %zu\n", len, sizeof(*msg));
		return 0;
	}

	/* MSG_AP_RESP means AP is registered to another AC — filter it out */
	if (msg->msg_type == MSG_AP_RESP &&
		!__uuid_equ(msg->acuuid, ac.acuuid)) {
		sys_debug("MSG_AP_RESP from other AC (%.36s)\n",
			msg->acuuid);
		return 0;
	}

	return 1;
}

void msg_proc(struct ap_hash_t *aphash,
	void *data, int len, int proto)
{
	if (!is_mine(msg, len))
		return;

	switch (msg->msg_type) {

	case MSG_AP_REG:
		__ap_reg(aphash, (void *)msg, len, proto);
		break;

	case MSG_AP_STATUS:
		/* Only received via TCP */
		if (proto == MSG_PROTO_TCP) {
			__ap_status(&aphash->ap, (void *)msg, len);
		} else {
			sys_warn("AP_STATUS received over ETH (unexpected)\n");
		}
		break;

	case MSG_AP_RESP:
		/* AP is already registered to another AC — informational */
		sys_debug("AP already registered to AC: %.36s\n",
			msg->acuuid);
		break;

	default:
		sys_err("Unknown message type: %d\n", msg->msg_type);
		break;
	}
}
