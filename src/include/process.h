/*
 * =====================================================================================
 *
 *       Filename:  process.h
 *
 *    Description:  Process declarations for both AC and AP.
 *                  AC identity (ac_t), AP state (sysstat_t), message processing.
 *
 *        Version:  2.1
 *       Created:  2026-04-12
 *       Revision:  2026-04-15 — added ac_t, ac_init, ap_lost, msg_proc_ac
 *
 * =====================================================================================
 */
#ifndef __PROCESS_H__
#define __PROCESS_H__

#include <linux/if_ether.h>
#include <netinet/in.h>
#include <pthread.h>
#include <time.h>

#include "msg.h"
#include "apstatus.h"
#include "aphash.h"     /* for struct ap_hash_t */
#include "message.h"    /* for struct message_t */

/* ========================================================================
 * AC identity — holds AC UUID, random challenge, and lock
 * ======================================================================== */

struct ac_t {
	char            acuuid[UUID_LEN];
	uint32_t        random;
	pthread_mutex_t lock;
};

#ifdef SERVER
extern struct ac_t ac;

void ac_init(void);
void ap_lost(int sock);
void msg_proc(struct ap_hash_t *aphash, void *data, int len, int proto);

/* AC message queue */
void ac_message_insert(struct ap_hash_t *aphash, struct message_t *msg);
void message_travel_init(void);
#endif

/* ========================================================================
 * AP-side state
 * ======================================================================== */

struct sysstat_t {
	char     acuuid[UUID_LEN];
	char     dmac[ETH_ALEN];
	int      isreg;
	int      sock;
	struct sockaddr_in server;
	pthread_mutex_t lock;
	time_t   last_brd;
};

extern struct sysstat_t sysstat;

/* ========================================================================
 * AP-side functions
 * ======================================================================== */

void init_report(void);
void ap_msg_proc(void *data, int len, int proto);
void *__net_netrcv(void *arg);

/* AP status collection */
unsigned long get_uptime(void);
unsigned long get_memfree(void);
unsigned int  get_cpu_usage(void);

#endif /* __PROCESS_H__ */
