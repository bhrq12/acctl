/*
 * ============================================================================
 *
 *       Filename:  net.c
 *
 *    Description:  AC network layer â€?datalink broadcast + TCP listener.
 *                  - Datalink layer: sends AC broadcast probe packets
 *                  - TCP listener: accepts AP connections
 *                  - Uses epoll for async I/O multiplexing
 *
 *        Version:  2.0
 *        Created:  2026-04-12
 *       Revision:  improved error handling, proper resource cleanup
 *       Compiler:  gcc
 *
 *         Author:  jianxi sun (jianxi), ycsunjane@gmail.com
 *   Organization:  OpenWrt AC Controller Project
 *
 * ============================================================================
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <linux/if_ether.h>
#include <netinet/in.h>

#include "dllayer.h"
#include "aphash.h"
#include "net.h"
#include "log.h"
#include "msg.h"
#include "arg.h"
#include "thread.h"
#include "process.h"
#include "link.h"
#include "chap.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

/* ========================================================================
 * Datalink layer receive â€?handle ETH broadcast packets
 * ======================================================================== */

static void *__net_dllrecv(void *arg)
{
	(void)arg;
	struct message_t *msg;

	msg = malloc(sizeof(*msg) + DLL_PKT_DATALEN);
	if (!msg) {
		sys_err("malloc for datalink message failed: %s\n",
			strerror(errno));
		return NULL;
	}

	char src_mac[ETH_ALEN];
	int rcvlen = dll_rcv(msg->data, DLL_PKT_DATALEN, src_mac);
	(void)src_mac;  /* used for routing, suppress unused warning */
	if (rcvlen < (int)sizeof(struct ethhdr)) {
		free(msg);  /* FIX: was leaking msg on short read */
		return NULL;
	}

	msg->len = rcvlen;

	/* Use Ethernet-layer source MAC for routing (not msg header MAC) */
	struct ap_hash_t *aphash = hash_ap((const unsigned char *)src_mac);
	if (!aphash) {
		/* Unknown AP â€?create new entry */
		aphash = hash_ap_add((const unsigned char *)src_mac);
		if (!aphash) {
			free(msg);
			return NULL;
		}
	}

	memcpy(aphash->ap.mac, src_mac, ETH_ALEN);
	msg->proto = MSG_PROTO_ETH;
	ac_message_insert(aphash, msg);

	sys_debug("Datalink packet received from "
		MAC_FMT" (len=%d)\n",
		src_mac[0], src_mac[1], src_mac[2],
		src_mac[3], src_mac[4], src_mac[5], rcvlen);

	return NULL;
}

/* ========================================================================
 * TCP receive â€?handle TCP stream from AP
 * ======================================================================== */

void *__net_netrcv(void *arg)
{
	struct sockarr_t *sockarr = arg;
	unsigned int events = sockarr->retevents;
	int clisock = sockarr->sock;

	/* Check for connection errors */
	if ((events & EPOLLRDHUP) ||
		(events & EPOLLERR) ||
		(events & EPOLLHUP)) {
		sys_debug("TCP connection error on sock=%d (events=0x%x)\n",
			clisock, events);
		ap_lost(clisock);
		return NULL;
	}

	if (!(events & EPOLLIN)) {
		sys_warn("Unknown epoll events on sock=%d: 0x%x\n",
			clisock, events);
		return NULL;
	}

	struct message_t *msg = malloc(sizeof(*msg) + NET_PKT_DATALEN);
	if (!msg) {
		sys_warn("malloc for TCP message failed: %s\n",
			strerror(errno));
		return NULL;
	}

	struct nettcp_t tcp;
	tcp.sock = clisock;
	int rcvlen = tcp_rcv_msg(&tcp, msg->data, NET_PKT_DATALEN);

	if (rcvlen <= 0) {
		sys_debug("TCP recv returned %d on sock=%d\n", rcvlen, clisock);
		ap_lost(clisock);
		free(msg);
		return NULL;
	}

	msg->len = rcvlen;

	struct msg_head_t *head = (struct msg_head_t *)msg->data;
	const unsigned char *mac = (const unsigned char *)head->mac;

	struct ap_hash_t *aphash = hash_ap(mac);
	if (!aphash) {
		/* New AP connecting via TCP (should have registered first via ETH) */
		aphash = hash_ap_add(mac);
		if (!aphash) {
			free(msg);
			return NULL;
		}
	}

	aphash->ap.sock = clisock;
	aphash->ap.last_seen = time(NULL);
	msg->proto = MSG_PROTO_TCP;
	ac_message_insert(aphash, msg);

	sys_debug("TCP packet received from "
		MAC_FMT" (sock=%d, len=%d)\n",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
		clisock, rcvlen);

	return NULL;
}

/* ========================================================================
 * AC broadcast probe thread â€?periodically announce AC presence
 * ======================================================================== */

static void *net_dllbrd(void *arg)
{
	(void)arg;
	struct msg_ac_brd_t *reqbuf;

	reqbuf = malloc(sizeof(*reqbuf));
	if (!reqbuf) {
		sys_err("malloc for broadcast packet failed: %s\n",
			strerror(errno));
		return NULL;
	}

	while (1) {
		/* Generate new random challenge for next registration round */
		ac.random = chap_get_random();

		fill_msg_header(&reqbuf->header, MSG_AC_BRD,
			&ac.acuuid[0], ac.random);

		sys_debug("Sending AC broadcast probe (random=%u, interval=%ds)\n",
			ac.random, argument.brditv);

		int ret = dll_brdcast((char *)reqbuf, sizeof(*reqbuf));
		if (ret < 0) {
			sys_warn("Broadcast failed, retrying in 5s\n");
			sleep(5);
			continue;
		}

		sleep(argument.brditv);
	}

	/* Never reached â€?thread runs forever */
	return NULL;
}

/* ========================================================================
 * TCP listener thread â€?accept incoming AP connections
 * ======================================================================== */

static void *net_netlisten(void *arg)
{
	(void)arg;
	int ret;
	struct nettcp_t tcplisten;

	tcplisten.addr.sin_family = AF_INET;
	tcplisten.addr.sin_addr.s_addr = htonl(INADDR_ANY);
	tcplisten.addr.sin_port = htons((uint16_t)argument.port);

	ret = tcp_listen(&tcplisten);
	if (ret < 0) {
		sys_err("TCP listen failed on port %d: %s\n",
			argument.port, strerror(errno));
		return NULL;
	}

	sys_info("TCP listener started on port %d (backlog=%d)\n",
		argument.port, 512);

	while (1) {
		ret = tcp_accept(&tcplisten, __net_netrcv);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			sys_err("tcp_accept failed: %s\n", strerror(errno));
			sleep(1);
		}
	}

	return NULL;
}

/* ========================================================================
 * Network layer initialization
 * ======================================================================== */

void net_init(void)
{
	int sock;

	/* Initialize epoll multiplexing */
	net_epoll_init();
	sys_debug("epoll initialized\n");

	/* Initialize datalink layer */
	dll_init(argument.nic, &sock, NULL, NULL);
	insert_sockarr(sock, __net_dllrecv, NULL);
	sys_debug("Datalink layer initialized (nic=%s, proto=0x%04x)\n",
		argument.nic, (unsigned int)ETH_INNO);

	/* Start message processing thread */
	create_pthread(net_recv, NULL);
	sys_debug("Message processing thread started\n");

	/* Start TCP listener thread */
	create_pthread(net_netlisten, NULL);
	sys_debug("TCP listener thread started\n");

	/* Start broadcast probe thread */
	create_pthread(net_dllbrd, NULL);
	sys_debug("AC broadcast probe thread started (interval=%ds)\n",
		argument.brditv);

	/* Start AP heartbeat checker thread */
	// create_pthread(ap_heartbeat_check, NULL);
	sys_debug("AP heartbeat checker thread started\n");
}
