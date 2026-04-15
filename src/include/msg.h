/*
 * =====================================================================================
 *
 *       Filename:  msg.h
 *
 *    Description:  Protocol message definitions.
 *                  All messages use a common header with CHAP + UUID + MAC.
 *                  Extended with ACK, extended status, and new v2 message types.
 *
 *        Version:  2.0
 *        Created:  2026-04-12
 *       Revision:  complete rewrite with extended protocol
 *       Compiler:  gcc
 *
 * =====================================================================================
 */
#ifndef __MSG_H__
#define __MSG_H__

#include <stdint.h>
#include <string.h>
#include <time.h>
#include <netinet/in.h>
#include <linux/if_ether.h>

#include "chap.h"
#include "arg.h"

/* External reference to global argument struct (defined in arg.c) */
extern struct arg_t argument;

#define UUID_LEN  (50)

/* ========================================================================
 * Message type definitions
 * ======================================================================== */

#define MSG_TYPE_START         (1000)
#define MSG_TYPE_EXTENDED      (2000)

enum {
	/* AC → AP messages */
	MSG_AC_BRD          = MSG_TYPE_START,     /* 1000: AC broadcast probe */
	MSG_AC_CMD,                                 /* 1001: AC command to AP */
	MSG_AC_REG_RESP,                            /* 1002: AC registration response */

	/* AP → AC messages */
	MSG_AP_REG          = 1003,             /* AP registration request */
	MSG_AP_RESP         = 1005,             /* AP response (already registered elsewhere) */
	MSG_AP_STATUS       = 1006,             /* AP status report */

	/* v2 Extended messages */
	MSG_AP_REPORT_ACK   = MSG_TYPE_EXTENDED, /* 2000: AC acknowledges status report */
	MSG_FW_HEADER,                             /* 2001: Firmware upgrade header */
	MSG_FW_CHUNK,                              /* 2002: Firmware data chunk */
	MSG_FW_VERIFY,                             /* 2003: Firmware verification */
	MSG_FW_REBOOT,                             /* 2004: Trigger AP reboot for upgrade */
	MSG_AP_REBOOT,                             /* 2005: AP reboot notification */
	MSG_CHANNEL_SCAN,                          /* 2006: Channel scan request */
	MSG_CHANNEL_REPORT,                        /* 2007: Channel scan results */
	MSG_AP_STATS,                              /* 2008: Extended statistics */
	MSG_CONFIG_SYNC,                           /* 2009: Configuration sync */
	MSG_HEARTBEAT,                             /* 2010: Heartbeat keepalive */
};

/* ========================================================================
 * Common message header — all messages start with this
 * ======================================================================== */

struct msg_head_t {
	uint32_t random;         /* challenge/response random number */
	uint8_t  chap[CHAP_LEN];/* CHAP MD5 digest (16 bytes) */
	char     acuuid[UUID_LEN]; /* AC unique identifier */
	char     mac[ETH_ALEN];  /* AP MAC address */
	int      msg_type;       /* message type */
} __attribute__((packed));

/* ========================================================================
 * AC → AP: Broadcast probe
 * ======================================================================== */

struct msg_ac_brd_t {
	struct msg_head_t header;
	/* takeover field: if non-empty UUID, means this is a forced
	 * re-registration request (AP should disconnect from current AC
	 * and register to the AC in acuuid field) */
	char  takeover[UUID_LEN];
} __attribute__((packed));

/* ========================================================================
 * AC → AP: Command execution
 * ======================================================================== */

struct msg_ac_cmd_t {
	struct msg_head_t header;
	/* Command string, null-terminated.
	 * Maximum length: NET_PKT_DATALEN - sizeof(msg_ac_cmd_t) */
	char  cmd[];
} __attribute__((packed));

/* ========================================================================
 * AC → AP: Registration response
 * ======================================================================== */

struct msg_ac_reg_resp_t {
	struct msg_head_t header;
	struct sockaddr_in acaddr;   /* AC's IP address + port */
	struct sockaddr_in apaddr;   /* AP's assigned IP address */
} __attribute__((packed));

/* ========================================================================
 * AC → AP: Status report ACK
 * ======================================================================== */

struct msg_ack_t {
	struct msg_head_t header;
	uint64_t timestamp;          /* Server receipt timestamp */
} __attribute__((packed));

/* ========================================================================
 * AP → AC: Registration request
 * ======================================================================== */

struct msg_ap_reg_t {
	struct msg_head_t header;
	struct sockaddr_in ipv4;     /* AP's WAN IP (from DHCP or static) */
} __attribute__((packed));

/* AP RESP is same as msg_head_t (just the header, no extra fields) */
#define msg_ap_resp_t msg_head_t

/* ========================================================================
 * AP → AC: Status report
 * ======================================================================== */

struct msg_ap_status_t {
	struct msg_head_t header;
	/* Followed immediately by struct apstatus_t (variable length) */
	char  status[];
} __attribute__((packed));

/* ========================================================================
 * AP → AC: Heartbeat
 * ======================================================================== */

struct msg_heartbeat_t {
	struct msg_head_t header;
	uint64_t uptime_sec;
	uint32_t memfree_kb;
	uint32_t cpu_percent;
} __attribute__((packed));

/* ========================================================================
 * Helper: fill message header
 * ======================================================================== */

static inline void
fill_msg_header(struct msg_head_t *msg, int msgtype,
	const char *uuid, uint32_t random)
{
	memset(msg, 0, sizeof(*msg));
	msg->random = random;
	if (uuid)
		memcpy(msg->acuuid, uuid, UUID_LEN - 1);
	memcpy(msg->mac, argument.mac, ETH_ALEN);
	msg->msg_type = msgtype;
}

/* ========================================================================
 * Helper: get timestamp for anti-replay
 * ======================================================================== */

static inline uint64_t msg_timestamp_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t)ts.tv_sec << 32) | (uint32_t)ts.tv_nsec;
}

#endif /* __MSG_H__ */
