/*
 * ============================================================================
 *
 *       Filename:  process.c
 *
 *    Description:  AP-side message processing.
 *                  Handles AC broadcast, registration, commands.
 *
 *        Version:  2.0
 *        Created:  2026-04-12
 *       Compiler:  gcc
 *
 * ============================================================================
 */
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "msg.h"
#include "log.h"
#include "dllayer.h"
#include "netlayer.h"
#include "link.h"
#include "net.h"
#include "arg.h"
#include "thread.h"
#include "process.h"
#include "apstatus.h"
#include "sec.h"
#include "aphash.h"

#define SYSSTAT_LOCK()    pthread_mutex_lock(&sysstat.lock)
#define SYSSTAT_UNLOCK() pthread_mutex_unlock(&sysstat.lock)

/* AP global state */
struct sysstat_t {
	char     acuuid[UUID_LEN];
	char     dmac[ETH_ALEN];   /* AC's MAC address */
	int      isreg;             /* 0=unregistered, 1=registered */
	int      sock;              /* TCP socket to AC */
	struct sockaddr_in server;  /* AC's IP address */
	pthread_mutex_t lock;
	time_t   last_brd;          /* timestamp of last broadcast received */
};

static struct sysstat_t sysstat = {
	.acuuid = {0},
	.dmac = {0},
	.isreg = 0,
	.sock = -1,
	.server = {0},
	.lock = PTHREAD_MUTEX_INITIALIZER,
};

/* MAC 鈫?random challenge map (for CHAP) */
#define LOCAL_AC_MAX  (16)
struct mac_random_map_t {
	uint32_t random;
	char mac[ETH_ALEN];
	time_t ts;
};
static struct mac_random_map_t random_map[LOCAL_AC_MAX];
static int random_map_offset = 0;
static pthread_mutex_t random_map_lock = PTHREAD_MUTEX_INITIALIZER;

/* ========================================================================
 * Random challenge tracking (for CHAP verification)
 * ======================================================================== */

static uint32_t map_store_random(char *mac, uint32_t random)
{
	pthread_mutex_lock(&random_map_lock);
	int slot = random_map_offset;
	random_map[slot].random = random;
	memcpy(random_map[slot].mac, mac, ETH_ALEN);
	random_map[slot].ts = time(NULL);
	random_map_offset = (random_map_offset + 1) % LOCAL_AC_MAX;
	pthread_mutex_unlock(&random_map_lock);
	return random;
}

static uint32_t map_get_random(char *mac)
{
	pthread_mutex_lock(&random_map_lock);
	uint32_t r = 0;
	time_t now = time(NULL);
	for (int i = 0; i < LOCAL_AC_MAX; i++) {
		if (random_map[i].mac[0] != 0 &&
			memcmp(random_map[i].mac, mac, ETH_ALEN) == 0 &&
			now - random_map[i].ts < 300) {  /* 5 min window */
			r = random_map[i].random;
			break;
		}
	}
	pthread_mutex_unlock(&random_map_lock);
	return r;
}

/* ========================================================================
 * AC connection management
 * ======================================================================== */

static int ac_connect(void)
{
	if (sysstat.server.sin_addr.s_addr == 0) {
		return -1;
	}

	struct nettcp_t tcp;
	tcp.addr = sysstat.server;
	int sock = tcp_connect(&tcp);

	if (sock >= 0) {
		insert_sockarr(sock, __net_netrcv, NULL);
		sys_debug("Connected to AC at %s:%d (sock=%d)\n",
			inet_ntoa(sysstat.server.sin_addr),
			ntohs(sysstat.server.sin_port), sock);
	}

	return sock;
}

static void ac_disconnect(void)
{
	/* FIX: capture sock fd before releasing lock to avoid use-after-free */
	int sock_to_close = -1;
	SYSSTAT_LOCK();
	if (sysstat.sock >= 0) {
		sock_to_close = sysstat.sock;
		sysstat.sock = -1;
	}
	SYSSTAT_UNLOCK();
	if (sock_to_close >= 0) {
		delete_sockarr(sock_to_close);
	}
}

/* ========================================================================
 * Status reporting
 * ======================================================================== */

static void *report_apstatus(void *arg)
{
	(void)arg;
	struct apstatus_t *ap;
	char *buf;
	int bufsize;

	/* Build status report buffer */
	bufsize = (int)(sizeof(struct msg_ap_status_t) + sizeof(struct apstatus_t));
	buf = calloc(1, (size_t)bufsize);
	if (!buf) {
		sys_err("calloc for status report failed: %s\n", strerror(errno));
		return NULL;
	}

	fill_msg_header((void *)buf, MSG_AP_STATUS, NULL, 0);

	while (1) {
		int proto;
		int ret;

		SYSSTAT_LOCK();
		int connected = (sysstat.sock >= 0);
		SYSSTAT_UNLOCK();

		if (connected) {
			proto = MSG_PROTO_TCP;
		} else {
			/* Not connected 鈥?try reconnecting */
			SYSSTAT_LOCK();
			ac_disconnect();
			int new_sock = ac_connect();
			sysstat.sock = new_sock;
			SYSSTAT_UNLOCK();
			if (new_sock < 0) {
				sleep(argument.reportitv);
				continue;
			}
			proto = MSG_PROTO_TCP;
		}

		/* Gather AP status */
		ap = get_apstatus();
		if (ap) {
			memcpy(buf + sizeof(struct msg_ap_status_t),
				ap, sizeof(struct apstatus_t));
		}

		/* Send status via TCP (or ETH fallback) */
		SYSSTAT_LOCK();
		int sock = sysstat.sock;
		SYSSTAT_UNLOCK();

		ret = net_send(proto, sock, sysstat.dmac, buf, bufsize);

		if (ret <= 0 && proto == MSG_PROTO_TCP) {
			sys_debug("Status send failed, AC may be lost\n");
			SYSSTAT_LOCK();
			ac_disconnect();
			sysstat.isreg = 0;
			SYSSTAT_UNLOCK();
		}

		sleep(argument.reportitv);
	}

	/* Never reached */
	return NULL;
}

/* ========================================================================
 * AC broadcast handling (phase 1: registration discovery)
 * ======================================================================== */

static void proc_brd(struct msg_ac_brd_t *msg, int len)
{
	if ((size_t)len < sizeof(*msg)) {
		sys_warn("Received truncated broadcast packet\n");
		return;
	}

	SYSSTAT_LOCK();
	int already_reg = sysstat.isreg;
	char my_uuid[UUID_LEN];
	strncpy(my_uuid, sysstat.acuuid, UUID_LEN - 1);
	SYSSTAT_UNLOCK();

	if (!already_reg) {
		/* Phase 1: Not registered 鈥?send registration request */
		char *resp_buf = malloc(sizeof(struct msg_ap_reg_t));
		if (!resp_buf) {
			sys_err("malloc for registration failed: %s\n",
				strerror(errno));
			return;
		}

		uint32_t r1 = chap_get_random();
		map_store_random(msg->header.mac, r1);

		fill_msg_header((void *)resp_buf, MSG_AP_REG,
			msg->header.acuuid, r1);

		struct msg_ap_reg_t *reg = (void *)resp_buf;
		reg->ipv4 = argument.addr;

		/* Compute CHAP: md5sum1 = packet + random0_from_AC + password */
		chap_fill_msg_md5((void *)resp_buf,
			sizeof(struct msg_ap_reg_t), msg->header.random);

		net_send(MSG_PROTO_ETH, -1, msg->header.mac,
			resp_buf, (int)sizeof(struct msg_ap_reg_t));
		free(resp_buf);

		sys_debug("Sent AP_REG to AC "
			MAC_FMT"\n",
			msg->header.mac[0], msg->header.mac[1],
			msg->header.mac[2], msg->header.mac[3],
			msg->header.mac[4], msg->header.mac[5]);

	} else {
		/* Phase 2: Already registered
		 * If this is a different AC broadcasting, handle takeover */
		if (strncmp(msg->header.acuuid, my_uuid, UUID_LEN - 1) != 0) {
			/* Check if we should switch ACs */
			sys_warn("Received broadcast from different AC: "
				"%.36s (mine: %.36s)\n",
				msg->header.acuuid, my_uuid);
			/* TODO: implement AC priority / failover logic */
		}
	}
}

/* ========================================================================
 * Registration response handling
 * ======================================================================== */

static void proc_reg_resp(struct msg_ac_reg_resp_t *msg, int len)
{
	if ((size_t)len < sizeof(*msg)) {
		sys_warn("Received truncated registration response\n");
		return;
	}

	/* Verify CHAP: md5sum3 = packet + random1 + password */
	uint32_t r1 = map_get_random(msg->header.mac);
	if (chap_msg_cmp_md5((void *)msg, sizeof(*msg), r1) != 0) {
		sys_err("Registration response CHAP verification failed\n");
		return;
	}

	SYSSTAT_LOCK();
	strncpy(sysstat.acuuid, msg->header.acuuid, UUID_LEN - 1);
	memcpy(sysstat.dmac, msg->header.mac, ETH_ALEN);
	SYSSTAT_UNLOCK();

	/* Set local IP address if assigned */
	if (msg->apaddr.sin_addr.s_addr) {
		struct ifreq req;
		int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
		if (sockfd >= 0) {
			memset(&req, 0, sizeof(req));
			strncpy(req.ifr_name, argument.nic, IFNAMSIZ - 1);
			/* Delete existing address first to avoid conflicts */
			ioctl(sockfd, SIOCDIFADDR, &req);
			/* Set the new assigned address */
			req.ifr_addr = *(struct sockaddr *)&msg->apaddr;
			req.ifr_addr.sa_family = AF_INET;
			ioctl(sockfd, SIOCSIFADDR, &req);
			/* Bring interface up */
			req.ifr_flags = IFF_UP | IFF_RUNNING;
			ioctl(sockfd, SIOCSIFFLAGS, &req);
			close(sockfd);
			sys_info("IP address set: %s\n",
				inet_ntoa(msg->apaddr.sin_addr));
		}
	}

	/* Connect to AC via TCP */
	SYSSTAT_LOCK();
	ac_disconnect();
	sysstat.server = msg->acaddr;
	sysstat.server.sin_port = htons((uint16_t)argument.port);
	sysstat.sock = ac_connect();
	sysstat.isreg = 1;
	SYSSTAT_UNLOCK();

	sys_info("Registered with AC: %.36s (sock=%d)\n",
		msg->header.acuuid, sysstat.sock);
}

/* ========================================================================
 * Command execution (AC 鈫?AP)
 * ======================================================================== */

static void proc_exec_cmd(struct msg_ac_cmd_t *cmd, int len)
{
	sys_debug("Received command from AC: %.80s\n",
		cmd->cmd[0] ? cmd->cmd : "(empty)");

	/* Validate command before execution */
	if (sec_validate_command(cmd->cmd) != 0) {
		sys_warn("Command rejected by security policy: %.60s\n",
			cmd->cmd);
		return;
	}

	/* Log the command */
	sys_info("Executing command: %s\n", cmd->cmd);

	/* Execute with output capture */
	char output[1024];
	int status = sec_exec_command(cmd->cmd, output, sizeof(output));
	if (status == 0) {
		sys_debug("Command output: %s\n",
			output[0] ? output : "(no output)");
	}
	/* TODO: Send command result back to AC via TCP */
	(void)status;
}

/* ========================================================================
 * Message routing
 * ======================================================================== */

void ap_msg_proc(void *data, int len, int proto)
{
	struct msg_head_t *head = data;

	switch (head->msg_type) {

	case MSG_AC_BRD:
		proc_brd((void *)data, len);
		break;

	case MSG_AC_REG_RESP:
		proc_reg_resp((void *)data, len);
		break;

	case MSG_AC_CMD:
		proc_exec_cmd((void *)data, len);
		break;

	default:
		sys_warn("Unknown message type: %d\n", head->msg_type);
		break;
	}
}

/* ========================================================================
 * Network layer receive callback (TCP)
 * ======================================================================== */

void *__net_netrcv(void *arg)
{
	struct sockarr_t *sa = arg;
	char buf[NET_PKT_DATALEN];

	if ((sa->retevents & EPOLLRDHUP) ||
		(sa->retevents & EPOLLERR) ||
		(sa->retevents & EPOLLHUP)) {
		sys_debug("TCP connection to AC lost\n");
		SYSSTAT_LOCK();
		sysstat.sock = -1;
		sysstat.isreg = 0;
		SYSSTAT_UNLOCK();
		delete_sockarr(sa->sock);
		return NULL;
	}

	if (!(sa->retevents & EPOLLIN))
		return NULL;

	struct nettcp_t tcp;
	tcp.sock = sa->sock;
	int rcvlen = tcp_rcv(&tcp, buf, sizeof(buf));
	if (rcvlen <= 0) {
		SYSSTAT_LOCK();
		sysstat.sock = -1;
		sysstat.isreg = 0;
		SYSSTAT_UNLOCK();
		delete_sockarr(sa->sock);
		return NULL;
	}

	ap_msg_proc(buf, rcvlen, MSG_PROTO_TCP);
	return NULL;
}

/* ========================================================================
 * Initialization
 * ======================================================================== */

void init_report(void)
{
	SYSSTAT_LOCK();
	sysstat.server = argument.acaddr;
	sysstat.server.sin_port = htons((uint16_t)argument.port);
	SYSSTAT_UNLOCK();

	create_pthread(report_apstatus, NULL);
	sys_debug("Status reporting thread started (interval=%ds)\n",
		argument.reportitv);
}
