/*
 * =====================================================================================
 *
 *       Filename:  sec.h
 *
 *    Description:  Security layer public API declarations
 *
 *        Version:  2.0
 *        Created:  2026-04-12
 *       Compiler:  gcc
 *
 * =====================================================================================
 */
#ifndef __SEC_H__
#define __SEC_H__

#include <stdint.h>
#include <linux/if_ether.h>

#define SEC_MAX_CMD_LEN  (256)

enum sec_rate_type {
	RATE_REGISTRATION = 0,
	RATE_COMMAND      = 1,
};

/* 1. Command validation */
int  sec_validate_command(const char *cmd);
int  sec_exec_command(const char *cmd, char *output, size_t output_len);

/* 2. Replay protection */
int  sec_check_replay(uint32_t random, time_t timestamp);
void sec_record_random(uint32_t random);

/* 3. Rate limiting */
int  sec_rate_check(const char *mac, int type);

/* 4. AC trust (takeover prevention) */
void sec_ac_trust_add(const char *mac);
int  sec_ac_is_trusted(const char *mac);

/* 5. Cryptographic random */
int  sec_get_random_bytes(uint8_t *buf, size_t len);

/* 6. HMAC integrity */
void sec_compute_hmac(const uint8_t *data, size_t len,
                      const uint8_t *key, size_t key_len,
                      uint8_t *hmac_out);
int  sec_verify_hmac(const uint8_t *data, size_t len,
                     const uint8_t *key, size_t key_len,
                     const uint8_t *expected_hmac);

/* 7. Initialization */
int  sec_init(void);

/* 8. Password (used by chap.c) */
const char *sec_get_password(void);
int  sec_password_check(void);

#endif /* __SEC_H__ */
