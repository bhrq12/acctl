/*
 * ============================================================================
 *
 *       Filename:  resource.c
 *
 *    Description:  IP address pool management.
 *                  Thread-safe IP allocation from a configurable pool.
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
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "log.h"
#include "resource.h"
#include "sql.h"
#include "arg.h"
#include "mjson.h"

struct _ippool_t *ippool = NULL;
struct resource_cfg_t resource;

static const char *json_attrs[] = {
	"ip_start", "ip_end", "ip_mask", NULL
};

/* ========================================================================
 * IP allocation
 * ======================================================================== */

struct _ip_t *res_ip_alloc(struct sockaddr_in *addr, char *mac)
{
	if (!ippool)
		return NULL;

	struct _ip_t *new_ip = NULL;

	LOCK(&ippool->lock);

	if (!ippool->left && list_empty(&ippool->pool)) {
		sys_warn("IP pool exhausted\n");
		UNLOCK(&ippool->lock);
		return NULL;
	}

alloc_from_pool:
	/* If addr is specified and valid, try to allocate that specific IP */
	if (addr && addr->sin_addr.s_addr != 0) {
		list_for_each_entry(new_ip, &ippool->pool, list) {
			if (new_ip->ipv4.sin_addr.s_addr == addr->sin_addr.s_addr) {
				list_move(&new_ip->list, &ippool->alloc);
				memcpy(new_ip->apmac, mac, ETH_ALEN);
				ippool->left--;
				UNLOCK(&ippool->lock);
				sys_debug("Allocated requested IP: %s\n",
					inet_ntoa(new_ip->ipv4.sin_addr));
				return new_ip;
			}
		}
		/* Requested IP not in pool — fall through to auto-allocate */
	}

	/* Auto-allocate from available pool (FIFO) */
	if (!list_empty(&ippool->pool)) {
		new_ip = list_first_entry(&ippool->pool, struct _ip_t, list);
		list_move(&new_ip->list, &ippool->alloc);
		memcpy(new_ip->apmac, mac, ETH_ALEN);
		ippool->left--;
		UNLOCK(&ippool->lock);
		sys_debug("Auto-allocated IP: %s (pool left: %d)\n",
			inet_ntoa(new_ip->ipv4.sin_addr), ippool->left);
		return new_ip;
	}

	/* No IPs available */
	if (new_ip == NULL) {
		sys_warn("No available IPs in pool\n");
	}
	UNLOCK(&ippool->lock);
	return NULL;
}

int res_ip_conflict(struct sockaddr_in *addr, char *mac)
{
	if (!ippool || !addr || addr->sin_addr.s_addr == 0)
		return 0;

	LOCK(&ippool->lock);
	struct _ip_t *ip;

	list_for_each_entry(ip, &ippool->alloc, list) {
		if (ip->ipv4.sin_addr.s_addr == addr->sin_addr.s_addr) {
			int conflict = memcmp(mac, ip->apmac, ETH_ALEN) != 0;
			UNLOCK(&ippool->lock);
			return conflict;
		}
	}

	UNLOCK(&ippool->lock);
	return 0;
}

static int res_ip_repeat(struct sockaddr_in *addr)
{
	if (!ippool)
		return 0;

	struct _ip_t *pos;

	list_for_each_entry(pos, &ippool->pool, list) {
		if (pos->ipv4.sin_addr.s_addr == addr->sin_addr.s_addr)
			return 1;
	}

	list_for_each_entry(pos, &ippool->alloc, list) {
		if (pos->ipv4.sin_addr.s_addr == addr->sin_addr.s_addr)
			return 1;
	}

	return 0;
}

int res_ip_add(struct sockaddr_in *addr)
{
	if (!ippool)
		return -1;

	LOCK(&ippool->lock);

	if (res_ip_repeat(addr))
		goto err;

	struct _ip_t *ip = calloc(1, sizeof(struct _ip_t));
	if (!ip) {
		sys_err("calloc for IP record failed: %s\n", strerror(errno));
		goto err;
	}

	ip->ipv4 = *addr;
	list_add_tail(&ip->list, &ippool->pool);
	ippool->total++;
	ippool->left++;

	UNLOCK(&ippool->lock);
	return 0;

err:
	UNLOCK(&ippool->lock);
	return -1;
}

void res_ip_clear(void)
{
	if (!ippool)
		return;

	LOCK(&ippool->lock);

	struct _ip_t *pos, *tmp;

	list_for_each_entry_safe(pos, tmp, &ippool->pool, list) {
		list_del(&pos->list);
		free(pos);
	}

	list_for_each_entry_safe(pos, tmp, &ippool->alloc, list) {
		list_del(&pos->list);
		free(pos);
	}

	ippool->total = 0;
	ippool->left = 0;

	UNLOCK(&ippool->lock);
}

static void res_ip_init(void)
{
	ippool = calloc(1, sizeof(struct _ippool_t));
	if (!ippool) {
		sys_err("calloc for IP pool failed: %s\n", strerror(errno));
		exit(-1);
	}

	LOCK_INIT(&ippool->lock);
	INIT_LIST_HEAD(&ippool->pool);
	INIT_LIST_HEAD(&ippool->alloc);
	ippool->total = 0;
	ippool->left = 0;
}

/* ========================================================================
 * Pool reload from database
 * ======================================================================== */

static int res_ip_equ_bak(void)
{
	if (strcmp(resource.ip_start, resource.bak_start) ||
		strcmp(resource.ip_end,   resource.bak_end)   ||
		strcmp(resource.ip_mask,  resource.bak_mask)) {
		strcpy(resource.bak_start, resource.ip_start);
		strcpy(resource.bak_end,   resource.ip_end);
		strcpy(resource.bak_mask,  resource.ip_mask);
		return 0;
	}
	return 1;  /* same as backup, no need to reload */
}

void res_ip_reload(void)
{
	char buffer[1024];

	/* Read resource config from database */
	if (sql_query_res(sql, buffer, sizeof(buffer)) != 0) {
		sys_err("Failed to read resource from database\n");
		return;
	}

	int status = json_read_object(buffer, json_attrs, NULL);
	if (status != 0) {
		sys_err("Failed to parse resource JSON: %s\n",
			json_error_string(status));
		return;
	}

	/* Check if pool changed */
	if (res_ip_equ_bak())
		return;  /* no change */

	sys_info("IP pool config changed, reloading...\n");
	sys_info("  Pool: %s - %s / %s\n",
		resource.ip_start, resource.ip_end, resource.ip_mask);

	struct in_addr ipstart, ipend, ipmask;

	if (!inet_aton(resource.ip_start, &ipstart) ||
		!inet_aton(resource.ip_end,   &ipend)   ||
		!inet_aton(resource.ip_mask,  &ipmask)) {
		sys_err("Invalid IP pool config: %s / %s / %s\n",
			resource.ip_start, resource.ip_end, resource.ip_mask);
		return;
	}

	uint32_t start_n = ntohl(ipstart.s_addr);
	uint32_t end_n   = ntohl(ipend.s_addr);
	uint32_t mask_n  = ntohl(ipmask.s_addr);

	uint32_t net_bits = ~mask_n;
	uint32_t start_host = start_n & mask_n;
	uint32_t end_host   = end_n   & mask_n;

	if (end_host <= start_host || net_bits == 0) {
		sys_warn("Invalid IP range\n");
		return;
	}

	int num = (int)(end_host - start_host + 1);

	/* Skip network address */
	if (start_host == 0)
		start_host++;

	/* Skip broadcast address */
	uint32_t max_host = net_bits;
	if (end_host >= max_host)
		end_host = max_host - 1;

	num = (int)(end_host - start_host + 1);
	if (num <= 0) {
		sys_warn("No usable IPs in pool\n");
		return;
	}

	res_ip_clear();

	uint32_t cur = start_host;
	char ip_str[INET_ADDRSTRLEN];
	struct sockaddr_in addr;

	for (int i = 0; i < num; i++) {
		addr.sin_addr.s_addr = htonl((cur + i) | (start_n & mask_n));
		inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str));
		if (res_ip_add(&addr) != 0)
			break;
	}

	sys_info("IP pool loaded: %d addresses\n", ippool->total);
}

/* ========================================================================
 * Periodic pool refresh thread
 * ======================================================================== */

void *res_check(void *arg)
{
	char buffer[1024];
	(void)arg;

	while (1) {
		sys_debug("Checking IP pool from database (interval=%ds)\n",
			argument.reschkitv);

		if (sql_query_res(sql, buffer, sizeof(buffer)) == 0) {
			int status = json_read_object(buffer, json_attrs, NULL);
			if (status == 0) {
				res_ip_reload();
			} else {
				sys_err("JSON parse error: %s\n",
					json_error_string(status));
			}
		}

		sleep(argument.reschkitv);
	}

	return NULL;
}

/* ========================================================================
 * Initialization
 * ======================================================================== */

void resource_init(void)
{
	/* Initialize pool structure */
	res_ip_init();

	/* Load initial pool from database */
	res_ip_reload();

	/* Start periodic refresh thread */
	create_pthread(res_check, NULL);

	sys_info("Resource manager initialized (total=%d, left=%d)\n",
		ippool->total, ippool->left);
}
