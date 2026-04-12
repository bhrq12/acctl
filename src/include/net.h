/*
 * =====================================================================================
 *
 *       Filename:  net.h
 *
 *    Description:  Network layer abstraction header
 *
 *        Version:  2.0
 *        Created:  2026-04-12
 *       Revision:  complete rewrite for production use
 *       Compiler:  gcc
 *
 *         Author:  jianxi sun (jianxi), ycsunjane@gmail.com
 *   Organization:  OpenWrt AC Controller Project
 *
 * =====================================================================================
 */
#ifndef __NET_H__
#define __NET_H__

#include <netinet/in.h>
#include <linux/if_ether.h>
#include "link.h"

#define MSG_PROTO_ETH     (8000)
#define MSG_PROTO_TCP     (8001)

int net_send(int proto, int sock, char *dmac, char *msg, int size);
void *net_recv(void *arg);

#endif /* __NET_H__ */
