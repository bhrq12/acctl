/*
 * =====================================================================================
 *       Filename:  chap.h
 *       Description:  CHAP authentication declarations
 * =====================================================================================
 */
#ifndef __CHAP_H__
#define __CHAP_H__

#include <stdint.h>
#include "md5.h"

#define CHAP_LEN  16

struct msg_head_t;

void chap_get_md5(uint8_t *data, int len, uint32_t random, uint8_t *decrypt);
int  chap_cmp_md5(uint8_t *data, int len, uint32_t random, uint8_t *oldmd5);
int  chap_msg_cmp_md5(struct msg_head_t *msg, int len, uint32_t random);
void chap_fill_msg_md5(struct msg_head_t *msg, int len, int random);
uint32_t chap_get_random(void);

/* Password management (v2.0 — no hardcoded password) */
const char *sec_get_password(void);
int  sec_password_check(void);

#endif /* __CHAP_H__ */
