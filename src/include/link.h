/*
 * =====================================================================================
 *
 *       Filename:  link.h
 *
 *    Description:  epoll-based I/O multiplexing and socket management.
 *                  Protocol constants are defined in net.h only.
 *
 *        Version:  2.0
 *       Revision:  2026-04-15 — removed duplicate MSG_PROTO enum
 *       Compiler:  gcc
 *
 *         Author:  jianxi sun (jianxi), ycsunjane@gmail.com
 *   Organization:  OpenWrt AC Controller Project
 *
 * =====================================================================================
 */
#ifndef __LINK_H__
#define __LINK_H__
#include <sys/epoll.h>
#include <stdint.h>
#include "dllayer.h"
#include "netlayer.h"
#include "net.h"

/*
 * NOTE: MSG_PROTO_ETH and MSG_PROTO_TCP are defined in net.h.
 * Do NOT redefine them here to avoid compilation conflicts.
 */

struct sockarr_t {
	struct epoll_event ev;

	int sock;
	uint32_t retevents;
	void *(*func) (void *);
	void *arg;

	struct sockarr_t *next;
};

void net_epoll_init(void);

int delete_sockarr(int sock);
struct sockarr_t * insert_sockarr(int sock, void *(*func) (void *), void *arg);

void * net_recv(void *arg);
int net_send(int proto, int sock, char *dmac, char *msg, int size);
#endif /* __LINK_H__ */
