/*
 * =====================================================================================
 *
 *       Filename:  aphash.h
 *
 *    Description:  AP hash table management declarations
 *
 *        Version:  2.0
 *        Created:  2026-04-12
 *       Revision:  rehashing, thread-safe, extended with group/tags
 *       Compiler:  gcc
 *
 *         Author:  jianxi sun (jianxi), ycsunjane@gmail.com
 *   Organization:  OpenWrt AC Controller Project
 *
 * =====================================================================================
 */
#ifndef __APHASH_H__
#define __APHASH_H__

#include <linux/if_ether.h>
#include <pthread.h>
#include <time.h>
#include "list.h"

#define AP_HASH_SIZE  (256)
#define MAC_FMT        "%02x:%02x:%02x:%02x:%02x:%02x"

#define AP_STATUS_UNKNOWN   (0)
#define AP_STATUS_ONLINE   (1)
#define AP_STATUS_OFFLINE  (2)
#define AP_STATUS_UPGRADING (3)

/* AP runtime state */
struct ap_t {
	char mac[ETH_ALEN];
	char uuid[50];
	char hostname[64];
	int  sock;                   /* TCP socket fd, -1 if offline */
	uint32_t random;             /* challenge from AP registration */
	time_t last_seen;            /* timestamp of last activity */
	int  status;                 /* AP_STATUS_* */
	int  group_id;               /* AP group membership */
	char tags[256];              /* JSON array of tags */
	char wan_ip[32];
	char wifi_ssid[64];
	char firmware[32];
	int  online_users;
	pthread_mutex_t lock;
	struct hlist_node node;
};

/* Hash bucket entry */
struct ap_hash_t {
	struct ap_t ap;
	struct message_t *msg_head;
	struct message_t **msg_tail;
	pthread_mutex_t msg_lock;
	struct hlist_node node;
};

/* Hash table */
struct ap_hash_table {
	struct hlist_head buckets[AP_HASH_SIZE];
	pthread_mutex_t lock;
	int count;
};

extern struct ap_hash_table g_ap_table;

void  hash_init(void);
struct ap_hash_t *hash_ap(char *mac);
struct ap_hash_t *hash_ap_add(char *mac);
void  hash_ap_del(char *mac);
void  hash_ap_update_sock(char *mac, int sock);
void  hash_ap_set_offline(char *mac);
int   hash_ap_count(void);
int   hash_ap_list_json(char *buf, int buflen);
void  hash_ap_dump(void);

#endif /* __APHASH_H__ */
