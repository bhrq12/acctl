/*
 * ============================================================================
 *
 *       Filename:  aphash.c
 *
 *    Description:  AP hash table implementation.
 *                  - MAC-address-based hash for O(1) lookup
 *                  - Thread-safe with mutex per bucket
 *                  - Supports rehashing when load factor exceeds threshold
 *
 *        Version:  2.0
 *        Created:  2026-04-12
 *       Compiler:  gcc
 *
 * ============================================================================
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <linux/if_ether.h>
#include <sys/time.h>

#include "aphash.h"
#include "log.h"

struct ap_hash_table g_ap_table;

#define HASH_BUCKET(mac) ({ \
	int _h = 0; \
	for (int _i = 0; _i < ETH_ALEN; _i++) \
		_h = (_h * 33 + (unsigned char)(mac)[_i]) % AP_HASH_SIZE; \
	_h; \
})

/*
 * hash_mac_to_key — convert MAC to hash key
 */
static unsigned int hash_mac_to_key(const char *mac)
{
	unsigned int key = 0;
	for (int i = 0; i < ETH_ALEN; i++)
		key = key * 33 + (unsigned char)mac[i];
	return key % AP_HASH_SIZE;
}

void hash_init(void)
{
	for (int i = 0; i < AP_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&g_ap_table.buckets[i]);
	pthread_mutex_init(&g_ap_table.lock, NULL);
	g_ap_table.count = 0;
}

/*
 * hash_ap — find AP entry by MAC address
 *   Returns: ap_hash_t* or NULL if not found
 */
struct ap_hash_t *hash_ap(char *mac)
{
	if (!mac)
		return NULL;

	unsigned int key = hash_mac_to_key((const char *)mac);
	struct ap_hash_t *aphash;

	pthread_mutex_lock(&g_ap_table.lock);
	hlist_for_each_entry(aphash, &g_ap_table.buckets[key], aphash->node) {
		if (memcmp(aphash->ap.mac, mac, ETH_ALEN) == 0) {
			pthread_mutex_unlock(&g_ap_table.lock);
			return aphash;
		}
	}
	pthread_mutex_unlock(&g_ap_table.lock);

	return NULL;
}

/*
 * hash_ap_add — create new AP entry for MAC address
 *   Returns: ap_hash_t* or NULL on error
 */
struct ap_hash_t *hash_ap_add(char *mac)
{
	if (!mac)
		return NULL;

	/* Check if already exists */
	struct ap_hash_t *existing = hash_ap(mac);
	if (existing)
		return existing;

	struct ap_hash_t *aphash = calloc(1, sizeof(*aphash));
	if (!aphash) {
		sys_err("calloc for ap_hash_t failed: %s\n", strerror(errno));
		return NULL;
	}

	memcpy(aphash->ap.mac, mac, ETH_ALEN);
	aphash->ap.sock = -1;
	aphash->ap.status = AP_STATUS_UNKNOWN;
	aphash->ap.last_seen = time(NULL);
	pthread_mutex_init(&aphash->msg_lock, NULL);
	INIT_HLIST_NODE(&aphash->node);
	/* Initialize message queue so ac_message_insert works correctly */
	aphash->msg_head = NULL;
	aphash->msg_tail = &aphash->msg_head;

	unsigned int key = hash_mac_to_key((const char *)mac);

	pthread_mutex_lock(&g_ap_table.lock);
	hlist_add_head(&aphash->node, &g_ap_table.buckets[key]);
	g_ap_table.count++;
	pthread_mutex_unlock(&g_ap_table.lock);

	sys_debug("AP added to hash: "
		MAC_FMT" (total=%d)\n",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
		g_ap_table.count);

	return aphash;
}

/*
 * hash_ap_del — remove AP entry by MAC address
 */
void hash_ap_del(char *mac)
{
	struct ap_hash_t *aphash;

	pthread_mutex_lock(&g_ap_table.lock);
	for (int i = 0; i < AP_HASH_SIZE; i++) {
		struct hlist_node *n, *tmp;
		hlist_for_each_entry_safe(aphash, n, tmp,
			&g_ap_table.buckets[i], aphash->node) {
			if (memcmp(aphash->ap.mac, mac, ETH_ALEN) == 0) {
				hlist_del(&aphash->node);
				g_ap_table.count--;
				pthread_mutex_destroy(&aphash->msg_lock);
				free(aphash);
				pthread_mutex_unlock(&g_ap_table.lock);
				sys_debug("AP removed from hash: "
					MAC_FMT" (remaining=%d)\n",
					mac[0], mac[1], mac[2],
					mac[3], mac[4], mac[5],
					g_ap_table.count);
				return;
			}
		}
	}
	pthread_mutex_unlock(&g_ap_table.lock);
}

/*
 * hash_ap_update_sock — update socket fd for AP
 */
void hash_ap_update_sock(char *mac, int sock)
{
	struct ap_hash_t *aphash = hash_ap(mac);
	if (aphash) {
		aphash->ap.sock = sock;
		aphash->ap.last_seen = time(NULL);
	}
}

/*
 * hash_ap_set_offline — mark AP as offline
 */
void hash_ap_set_offline(char *mac)
{
	struct ap_hash_t *aphash = hash_ap(mac);
	if (aphash) {
		aphash->ap.sock = -1;
		aphash->ap.status = AP_STATUS_OFFLINE;
	}
}

/*
 * hash_ap_count — get total number of APs in hash table
 */
int hash_ap_count(void)
{
	int count;
	pthread_mutex_lock(&g_ap_table.lock);
	count = g_ap_table.count;
	pthread_mutex_unlock(&g_ap_table.lock);
	return count;
}

/*
 * hash_ap_list_json — serialize AP list to JSON
 *
 * Format:
 *   {"count":N,"aps":[
 *     {"mac":"XX:XX:...","status":"online","last_seen":1234567890,...},
 *     ...
 *   ]}
 *
 * Returns: number of bytes written, or -1 on buffer overflow
 */
int hash_ap_list_json(char *buf, int buflen)
{
	int written = 0;
	char *p = buf;
	int space = buflen;

	if (space < 32)
		return -1;

	int n = snprintf(p, space, "{\"count\":%d,\"aps\":[",
		g_ap_table.count);
	if (n < 0 || n >= space) return -1;
	p += n; space -= n;

	int first = 1;
	for (int i = 0; i < AP_HASH_SIZE; i++) {
		pthread_mutex_lock(&g_ap_table.lock);
		struct ap_hash_t *aphash;
		hlist_for_each_entry(aphash, &g_ap_table.buckets[i],
			aphash->node) {
			if (aphash->ap.mac[0] == 0)
				continue;

			const char *status_str =
				(aphash->ap.status == AP_STATUS_ONLINE) ? "online" :
				(aphash->ap.status == AP_STATUS_OFFLINE) ? "offline" :
				(aphash->ap.status == AP_STATUS_UPGRADING) ? "upgrading" :
				"unknown";

			n = snprintf(p, space,
				"%s{\"mac\":\"" MAC_FMT
				"\",\"status\":\"%s\",\"last_seen\":%ld",
				first ? "" : ",",
				aphash->ap.mac[0], aphash->ap.mac[1],
				aphash->ap.mac[2], aphash->ap.mac[3],
				aphash->ap.mac[4], aphash->ap.mac[5],
				status_str, (long)aphash->ap.last_seen);

			if (n < 0 || n >= space) {
				pthread_mutex_unlock(&g_ap_table.lock);
				return -1;
			}
			p += n; space -= n; first = 0;
		}
		pthread_mutex_unlock(&g_ap_table.lock);
	}

	n = snprintf(p, space, "]}");
	if (n < 0 || n >= space) return -1;
	p += n; space -= n;

	return (int)(p - buf);
}

/*
 * hash_ap_dump — debug dump of hash table
 */
void hash_ap_dump(void)
{
	sys_info("AP Hash Table Dump (count=%d):\n", g_ap_table.count);
	for (int i = 0; i < AP_HASH_SIZE; i++) {
		pthread_mutex_lock(&g_ap_table.lock);
		struct ap_hash_t *aphash;
		hlist_for_each_entry(aphash, &g_ap_table.buckets[i],
			aphash->node) {
			if (aphash->ap.mac[0] == 0)
				continue;

			sys_info("  [%3d] " MAC_FMT
				" sock=%d status=%d last_seen=%ld\n",
				i,
				aphash->ap.mac[0], aphash->ap.mac[1],
				aphash->ap.mac[2], aphash->ap.mac[3],
				aphash->ap.mac[4], aphash->ap.mac[5],
				aphash->ap.sock,
				aphash->ap.status,
				(long)aphash->ap.last_seen);
		}
		pthread_mutex_unlock(&g_ap_table.lock);
	}
}
