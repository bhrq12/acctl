/*
 * ============================================================================
 *       Filename:  net.c
 *       Description:  AP-side network layer â€?datalink receive
 * ============================================================================
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/if_ether.h>

#include "dllayer.h"
#include "log.h"
#include "msg.h"
#include "arg.h"
#include "thread.h"
#include "process.h"
#include "link.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/*
 * Datalink layer receive callback.
 * This function is registered with epoll and called when ETH packets arrive.
 * ETH packets are always MSG_AC_BRD (AC broadcast probe).
 */
static void *__net_dllrecv(void *arg)
{
	(void)arg;

	char *buf = malloc(DLL_PKT_DATALEN);
	if (!buf) {
		sys_err("malloc for ETH recv failed: %s\n", strerror(errno));
		return NULL;
	}

	char src_mac[ETH_ALEN];
	int rcvlen = dll_rcv(buf, DLL_PKT_DATALEN, src_mac);
	if (rcvlen <= 0) {
		free(buf);
		return NULL;
	}

	if ((size_t)rcvlen < sizeof(struct msg_head_t)) {
		sys_warn("Truncated ETH packet received (len=%d)\n", rcvlen);
		free(buf);
		return NULL;
	}

	struct msg_head_t *head = (void *)buf;

	/* Only process AC broadcast probe messages on ETH */
	if (head->msg_type == MSG_AC_BRD) {
		/* Store AC's MAC from ETH header for TCP connection */
		memcpy(sysstat.dmac, src_mac, ETH_ALEN);
		/* Pass directly to message processor (single-threaded AP model) */
		ap_msg_proc(buf, rcvlen, MSG_PROTO_ETH);
	}

	free(buf);
	return NULL;
}

void net_init(void)
{
	int sock;

	/* Initialize epoll */
	net_epoll_init();

	/* Initialize datalink layer and get receive socket */
	dll_init(argument.nic, &sock, NULL, NULL);

	/* Register ETH receive socket with epoll */
	insert_sockarr(sock, __net_dllrecv, NULL);

	/* Start the epoll event loop thread */
	create_pthread(net_recv, NULL);

	sys_debug("AP network layer initialized (nic=%s, ETH proto=0x%04x)\n",
		argument.nic, (unsigned int)ETH_INNO);
}
