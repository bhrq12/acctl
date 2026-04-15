/*
 * ============================================================================
 *
 *       Filename:  netlayer.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  2014年08月26日 15时55分51秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  jianxi sun (jianxi), ycsunjane@gmail.com
 *   Organization:  
 *
 * ============================================================================
 */
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>
#include <fcntl.h>
#include <signal.h>

#include "log.h"
#include "link.h"
#include "netlayer.h"
#include "msg.h"
#include "apstatus.h"

static int __tcp_alive(struct nettcp_t *tcp)
{

	int optval = 1;
	int optlen = sizeof(optval);
	if(setsockopt(tcp->sock, SOL_SOCKET, 
			SO_KEEPALIVE, &optval, optlen) < 0) {
		sys_err("Set tcp keepalive failed: %s\n",
			strerror(errno));
		return -1;
	}

	/* TCP_KEEPCNT: max probe attempts before giving up.
	 * Default was dangerously high (30). Set to 3 for faster
	 * dead-connection detection. Total detection time:
	 *   KEEPIDLE(60s) + KEEPCNT(3) * KEEPINTVL(10s) = 90s max */
	optval = 3;
	if(setsockopt(tcp->sock, SOL_TCP, 
			TCP_KEEPCNT, &optval, optlen) < 0) {
		sys_err("Set tcp_keepalive_probes failed: %s\n",
			strerror(errno));
		return -1;
	}

	/* TCP_KEEPIDLE: time before first probe (seconds of inactivity).
	 * 60s is reasonable — detects dead connections within 90s total. */
	optval = 60;
	if(setsockopt(tcp->sock, SOL_TCP, 
			TCP_KEEPIDLE, &optval, optlen) < 0) {
		sys_err("Set tcp_keepalive_time failed: %s\n",
			strerror(errno));
		return -1;
	}

	/* TCP_KEEPINTVL: interval between probes. */
	optval = 10;
	if(setsockopt(tcp->sock, SOL_TCP, 
			TCP_KEEPINTVL, &optval, optlen) < 0) {
		sys_err("Set tcp_keepalive_intvl failed: %s\n",
			strerror(errno));
		return -1;
	}

	return 0;
}

int tcp_connect(struct nettcp_t *tcp)
{
	int ret;
	if(tcp->addr.sin_addr.s_addr == 0)
		return -1;

	tcp->sock = socket(AF_INET, SOCK_STREAM, 0);
	if(tcp->sock < 0) {
		sys_err("Create tcp sock failed: %s\n",
			strerror(errno));
		return -1;
	}

	ret = __tcp_alive(tcp);
	if(ret < 0) {
		sys_err("Set tcp alive failed\n");
		tcp_close(tcp);
		return -1;
	}

	socklen_t addr_len = sizeof(struct sockaddr_in);
	ret = connect(tcp->sock, (struct sockaddr *)&tcp->addr, addr_len);
	if(ret < 0) {
		tcp_close(tcp);
		sys_err("Connect ac failed: %s\n", strerror(errno));
		return -1;
	}

	return tcp->sock;
}

int tcp_rcv(struct nettcp_t *tcp, char *data, int size)
{
	assert(data != NULL && tcp->sock >= 0);

	int recvlen = 0, len;

	while(1) {
		len = recv(tcp->sock, data, size, 0);
		if(len > 0) {
			data += len;
			size -= len;
			recvlen += len;
			if(size <= 0)
				break;
			continue;
		} else if(len < 0) {
			if(errno == EINTR)
				continue;
			if(errno == EAGAIN)
				break;
			sys_err("sock: %d, tcp recv failed: %s(%d)\n",
				tcp->sock, strerror(errno), errno);
			break;
		}
		/* len == 0: peer closed connection */
		break;
	}

	sys_debug("Recv msg from sock: %d, size: %d\n", tcp->sock, recvlen);
	return recvlen;
}

/*
 * tcp_rcv_msg — protocol-aware TCP message receive
 *
 * Reads a single complete message from the TCP stream based on the
 * protocol header. First reads msg_head_t to determine message type,
 * then reads the remaining payload based on the expected message size.
 *
 * Returns: total bytes received (header + payload), or -1 on error.
 */
int tcp_rcv_msg(struct nettcp_t *tcp, char *data, int bufsize)
{
	assert(data != NULL && tcp->sock >= 0);

	int hdrlen = (int)sizeof(struct msg_head_t);
	int received = 0;

	/* Phase 1: Read the fixed message header */
	while (received < hdrlen) {
		int len = recv(tcp->sock, data + received,
			hdrlen - received, 0);
		if (len > 0) {
			received += len;
		} else if (len < 0) {
			if (errno == EINTR) continue;
			if (errno == EAGAIN) break;
			return -1;
		} else {
			return (received > 0) ? received : -1;
		}
	}

	if (received < hdrlen)
		return received;

	/* Phase 2: Determine expected message length from header */
	struct msg_head_t *head = (struct msg_head_t *)data;
	int msg_len = hdrlen;  /* minimum: header only */

	switch (head->msg_type) {
	case MSG_AC_BRD:
		msg_len = (int)sizeof(struct msg_ac_brd_t);
		break;
	case MSG_AC_REG_RESP:
		msg_len = (int)sizeof(struct msg_ac_reg_resp_t);
		break;
	case MSG_AP_REG:
		msg_len = (int)sizeof(struct msg_ap_reg_t);
		break;
	case MSG_AP_STATUS:
		msg_len = hdrlen + (int)sizeof(struct apstatus_t);
		break;
	case MSG_AP_REPORT_ACK:
		msg_len = (int)sizeof(struct msg_ack_t);
		break;
	case MSG_HEARTBEAT:
		msg_len = (int)sizeof(struct msg_heartbeat_t);
		break;
	default:
		/* FIX: unknown message type -- log and return error */
		sys_warn("tcp_rcv_msg: unknown msg_type=%d, dropping packet\n",
			head->msg_type);
		return -1;
	}

	if (msg_len > bufsize)
		msg_len = bufsize;

	/* Phase 3: Read remaining payload */
	while (received < msg_len) {
		int len = recv(tcp->sock, data + received,
			msg_len - received, 0);
		if (len > 0) {
			received += len;
		} else if (len < 0) {
			if (errno == EINTR) continue;
			if (errno == EAGAIN) break;
			break;
		} else {
			break;
		}
	}

	sys_debug("Recv msg from sock: %d, type=%d, size: %d\n",
		tcp->sock, head->msg_type, received);
	return received;
}

int tcp_sendpkt(struct nettcp_t *tcp, char *data, int size)
{
	assert(data != NULL && size <= NET_PKT_DATALEN);

	if(tcp->sock == -1) return -1;

	int sdrlen;
	while(1) {
		sdrlen = send(tcp->sock, data, size, 0);
		if(sdrlen < 0) {
			if(errno == EAGAIN || errno == EINTR)
				continue;
			sys_err("sock: %d, tcp send failed: %s(%d)\n", 
				tcp->sock, strerror(errno), errno);
		}
		break;
	}

	sys_debug("Send packet success: %d\n", sdrlen);
	return sdrlen;
}

void tcp_close(struct nettcp_t *tcp)
{
	close(tcp->sock);
	tcp->sock = -1;
}

#ifdef SERVER
#define BACKLOG 	(512)
int tcp_listen(struct nettcp_t *tcp)
{
	int ret;

	tcp->sock = socket(AF_INET, SOCK_STREAM, 0);
	if(tcp->sock < 0) {
		sys_err("Create tcp sock failed: %s\n",
			strerror(errno));
		return -1;
	}

	int reuseaddr = 1;
	ret = setsockopt(tcp->sock, SOL_SOCKET, SO_REUSEADDR,
		&reuseaddr, sizeof(reuseaddr));
	if(ret < 0) {
		sys_err("set sock reuse failed: %s\n",
			strerror(errno));
		return -1;
	}

	socklen_t socklen = sizeof(struct sockaddr_in);
	ret = bind(tcp->sock, 
		(struct sockaddr *)&tcp->addr, socklen);
	if(ret < 0) {
		sys_err("Bind tcp sock failed: %s\n",
			strerror(errno));
		tcp_close(tcp);
		return -1;
	}

	ret = listen(tcp->sock, BACKLOG);
	if(ret < 0) {
		sys_err("Bind tcp sock failed: %s\n",
			strerror(errno));
		tcp_close(tcp);
		return -1;
	}

	return tcp->sock;
}



int tcp_accept(struct nettcp_t *tcp, void *func(void *))
{
	int clisock;
	clisock = accept(tcp->sock, NULL, NULL);
	if(clisock < 0) {
		sys_err("Accept tcp sock failed: %s\n",
			strerror(errno));
		return -1;
	}

	sys_debug("New client:%d\n", clisock);
	insert_sockarr(clisock, func, NULL);
	return clisock;
}
#endif
