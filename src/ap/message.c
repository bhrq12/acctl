/*
 * ============================================================================
 *
 *       Filename:  message.c
 *
 *    Description:  AP-side message queue. Simplified — single-threaded.
 *
 *        Version:  2.0
 *        Created:  2026-04-12
 *       Compiler:  gcc
 *
 * ============================================================================
 */
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "arg.h"
#include "msg.h"
#include "thread.h"
#include "process.h"
#include "message.h"

struct message_t *head = NULL;
struct message_t **tail = &head;
pthread_mutex_t message_lock = PTHREAD_MUTEX_INITIALIZER;

#define MUTEX_LOCK()    pthread_mutex_lock(&message_lock)
#define MUTEX_UNLOCK()  pthread_mutex_unlock(&message_lock)

static int message_num = 0;

void message_insert(struct message_t *msg)
{
	MUTEX_LOCK();
	msg->next = NULL;
	message_num++;
	*tail = msg;
	tail = &msg->next;
	MUTEX_UNLOCK();
}

struct message_t *message_delete(void)
{
	MUTEX_LOCK();
	if (message_num == 0) {
		MUTEX_UNLOCK();
		return NULL;
	}

	struct message_t *tmp = head;
	head = head->next;
	message_num--;

	if (tail == &tmp->next)
		tail = &head;

	MUTEX_UNLOCK();
	return tmp;
}

void message_free(struct message_t *msg)
{
	if (msg) {
		if (msg->data)
			free(msg->data);
		free(msg);
	}
}

void *message_travel(void *arg)
{
	(void)arg;
	struct message_t *msg;

	while (1) {
		sleep(argument.msgitv);

		MUTEX_LOCK();
		int pending = message_num;
		MUTEX_UNLOCK();

		if (pending == 0)
			continue;

		while ((msg = message_delete()) != NULL) {
			ap_msg_proc(msg->data, msg->len, msg->proto);
			message_free(msg);
		}
	}

	return NULL;
}

void message_init(void)
{
	create_pthread(message_travel, NULL);
}
