/*
 * =====================================================================================
 *
 *       Filename:  resource.h
 *
 *    Description:  IP address pool management declarations
 *
 *        Version:  2.0
 *        Created:  2026-04-12
 *       Revision:  thread-safe with mutex protection
 *       Compiler:  gcc
 *
 *         Author:  jianxi sun (jianxi), ycsunjane@gmail.com
 *   Organization:  OpenWrt AC Controller Project
 *
 * =====================================================================================
 */
#ifndef __RESOURCE_H__
#define __RESOURCE_H__

#include <netinet/in.h>
#include <linux/if_ether.h>
#include <pthread.h>
#include "list.h"

#define IP_POOL_SIZE  (8192)

/* IP address record */
struct _ip_t {
	struct list_head list;
	struct sockaddr_in ipv4;
	char apmac[ETH_ALEN];
};

/* IP address pool */
struct _ippool_t {
	struct list_head pool;     /* available IPs */
	struct list_head alloc;     /* allocated IPs */
	pthread_mutex_t lock;
	int total;
	int left;
};

extern struct _ippool_t *ippool;

/* Pool management */
void  resource_init(void);
void *res_check(void *arg);

/* IP allocation */
struct _ip_t *res_ip_alloc(struct sockaddr_in *addr, char *mac);
int   res_ip_conflict(struct sockaddr_in *addr, char *mac);
int   res_ip_add(struct sockaddr_in *addr);
void  res_ip_clear(void);

/* Global resource config (from database) */
struct resource_cfg_t {
	char ip_start[32];
	char ip_end[32];
	char ip_mask[32];
	char bak_start[32];
	char bak_end[32];
	char bak_mask[32];
};

extern struct resource_cfg_t resource;

#endif /* __RESOURCE_H__ */
