/*
 * =====================================================================================
 *       Filename:  process.h
 *       Description:  AP-side process declarations — v2.0
 * =====================================================================================
 */
#ifndef __AP_PROCESS_H__
#define __AP_PROCESS_H__

#include <linux/if_ether.h>
#include <netinet/in.h>
#include <pthread.h>

#include "msg.h"
#include "apstatus.h"

/* Forward declaration — defined in process.c */
void msg_proc(void *data, int len, int proto);

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

void init_report(void);
void msg_proc(void *data, int len, int proto);
void *__net_netrcv(void *arg);

/* AP status collection (declared in apstatus.h) */
struct apstatus_t *get_apstatus(void);
unsigned long get_uptime(void);
unsigned long get_memfree(void);
unsigned int  get_cpu_usage(void);

#endif /* __AP_PROCESS_H__ */
