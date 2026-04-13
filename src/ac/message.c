/*
 * ============================================================================
 *
 *       Filename:  message.c
 *
 *    Description:  AC-side message queue.
 *                  Per-AP message ring buffers, processed by a dedicated thread.
 *                  Thread-safe using mutex locks.
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
#include <errno.h>

#include "log.h"
#include "arg.h"
#include "msg.h"
#include "thread.h"
#include "aphash.h"
#include "process.h"
#include "apstatus.h"

#define MSG_QUEUE_MAX  (256)

/*
 * ac_message_insert — insert message into AP's per-connection queue
 */
void ac_message_insert(struct ap_hash_t *aphash, struct message_t *msg)
{
	if (!aphash || !msg)
		return;

	msg->next = NULL;

	pthread_mutex_lock(&aphash->msg_lock);

	int count = 0;
	struct message_t **p = &aphash->msg_head;
	while (*p && count < MSG_QUEUE_MAX) {
		p = &(*p)->next;
		count++;
	}

	if (count >= MSG_QUEUE_MAX) {
		sys_warn("Message queue overflow for AP "
			MAC_FMT", dropping message\n",
			aphash->ap.mac[0], aphash->ap.mac[1],
			aphash->ap.mac[2], aphash->ap.mac[3],
			aphash->ap.mac[4], aphash->ap.mac[5]);
		pthread_mutex_unlock(&aphash->msg_lock);
		free(msg);
		return;
	}

	*p = msg;
	pthread_mutex_unlock(&aphash->msg_lock);
	sys_debug("Message queued for AP "
		MAC_FMT" (queue depth=%d)\n",
		aphash->ap.mac[0], aphash->ap.mac[1],
		aphash->ap.mac[2], aphash->ap.mac[3],
		aphash->ap.mac[4], aphash->ap.mac[5],
		count + 1);
}

/*
 * ac_message_delete — dequeue first message from AP's queue
 */
static struct message_t *ac_message_delete(struct ap_hash_t *aphash)
{
	if (!aphash)
		return NULL;

	pthread_mutex_lock(&aphash->msg_lock);
	struct message_t *msg = aphash->msg_head;
	if (msg) {
		aphash->msg_head = msg->next;
		if (!msg->next)
			aphash->msg_head = NULL;
		msg->next = NULL;
	}
	pthread_mutex_unlock(&aphash->msg_lock);

	return msg;
}

/*
 * ac_message_free — free a dequeued message
 */
static void ac_message_free(struct message_t *msg)
{
	if (msg) {
		if (msg->data)
			free(msg->data);
		free(msg);
	}
}

/*
 * ac_message_travel — process messages for all APs
 *
 * Loops through all hash buckets and processes pending messages
 * for each AP. This runs in a dedicated thread.
 */
static void *ac_message_travel(void *arg)
{
	(void)arg;
	struct message_t *msg;
	struct ap_hash_t *aphash;
	int processed;

	while (1) {
		processed = 0;

		/* Iterate all hash buckets */
		for (int i = 0; i < AP_HASH_SIZE; i++) {
			pthread_mutex_lock(&g_ap_table.lock);
			hlist_for_each_entry(aphash, &g_ap_table.buckets[i],
				aphash->node) {
				/* Skip empty slots */
				if (aphash->ap.mac[0] == 0)
					continue;

				/* Process all pending messages for this AP */
				while ((msg = ac_message_delete(aphash)) != NULL) {
					msg_proc(aphash, (void *)msg->data,
						msg->len, msg->proto);
					ac_message_free(msg);
					processed++;
				}
			}
			pthread_mutex_unlock(&g_ap_table.lock);
		}

		if (processed > 0) {
			sys_debug("Processed %d messages this round\n", processed);
		}

		/* Sleep before next processing cycle */
		sleep(argument.msgitv);
	}

	return NULL;
}

/*
 * message_travel_init — start the message processing thread
 */
void message_travel_init(void)
{
	create_pthread(ac_message_travel, NULL);
	sys_debug("Message travel thread initialized (interval=%ds)\n",
		argument.msgitv);
}
