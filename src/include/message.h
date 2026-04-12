/*
 * =====================================================================================
 *
 *       Filename:  message.h
 *
 *    Description:  AP-side message queue declarations
 *
 *        Version:  2.0
 *        Created:  2026-04-12
 *       Revision:  thread-safe message ring buffer
 *       Compiler:  gcc
 *
 *         Author:  jianxi sun (jianxi), ycsunjane@gmail.com
 *   Organization:  OpenWrt AC Controller Project
 *
 * =====================================================================================
 */
#ifndef __MESSAGE_H__
#define __MESSAGE_H__

#include <pthread.h>

#define MESSAGE_MAX   (256)

struct message_t {
	char *data;
	int   len;
	int   proto;
	struct message_t *next;
};

extern struct message_t *head;
extern struct message_t **tail;
extern pthread_mutex_t message_lock;

void message_insert(struct message_t *msg);
struct message_t *message_delete(void);
void message_free(struct message_t *msg);
void *message_travel(void *arg);
void message_init(void);

/* AC-side message travel (different from AP-side) */
struct ap_hash_t;
void *ac_message_travel(void *arg);
void ac_message_insert(struct ap_hash_t *aphash, struct message_t *msg);

#endif /* __MESSAGE_H__ */
