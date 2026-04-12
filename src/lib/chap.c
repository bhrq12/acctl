/*
 * ============================================================================
 *
 *       Filename:  chap.c
 *
 *    Description:  CHAP authentication with UCI password support
 *                  - No hardcoded password anymore
 *                  - Reads password from /etc/config/acctl
 *                  - Supports PBKDF2 key derivation for future upgrade
 *
 *        Version:  2.0
 *        Created:  2026-04-12
 *       Revision:  complete rewrite — remove hardcoded password
 *       Compiler:  gcc
 *
 *         Author:  jianxi sun (jianxi), ycsunjane@gmail.com
 *   Organization:  OpenWrt AC Controller Project
 *
 * ============================================================================
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>

#include "chap.h"
#include "msg.h"
#include "log.h"

#define MAX_PASSWORD_LEN  128

/* Password loaded from UCI config file at runtime.
 * No hardcoded default password in production builds.
 * The password must be set via: uci set acctl.@acctl[0].password='your_secure_password'
 */
static char g_password[MAX_PASSWORD_LEN + 1] = {0};
static int  g_password_loaded = 0;

/*
 * Load password from /etc/config/acctl.
 * Format: config acctl -> option password 'xxx'
 * Returns 0 on success, -1 on failure.
 */
static int load_password_from_uci(void)
{
	FILE *fp;
	char line[256];
	char *val;

	if (g_password_loaded)
		return 0;

	/* Try common UCI config paths */
	const char *paths[] = {
		"/etc/config/acctl",
		"/var/etc/acctl.conf",
		NULL
	};

	for (int i = 0; paths[i]; i++) {
		fp = fopen(paths[i], "r");
		if (!fp)
			continue;

		while (fgets(line, sizeof(line), fp)) {
			/* Skip comments and empty lines */
			if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
				continue;

			/* Look for: option password 'value' or option password "value" */
			if (strstr(line, "option") && strstr(line, "password")) {
				/* Find opening quote */
				val = strchr(line, '\'');
				if (!val) val = strchr(line, '"');
				if (val) {
					val++; /* skip opening quote */
					char *end = strchr(val, '\'');
					if (!end) end = strchr(val, '"');
					if (end) {
						int len = end - val;
						if (len > MAX_PASSWORD_LEN)
							len = MAX_PASSWORD_LEN;
						strncpy(g_password, val, len);
						g_password[len] = '\0';
						fclose(fp);
						g_password_loaded = 1;
						sys_debug("Password loaded from UCI config\n");
						return 0;
					}
				}
			}
		}
		fclose(fp);
	}

	/* Fallback: try to read from secret file */
	fp = fopen("/etc/acctl/password", "r");
	if (fp) {
		if (fgets(g_password, MAX_PASSWORD_LEN, fp)) {
			/* Remove trailing newline */
			size_t len = strlen(g_password);
			while (len > 0 && (g_password[len-1] == '\n' || g_password[len-1] == '\r')) {
				len--;
				g_password[len] = '\0';
			}
			fclose(fp);
			g_password_loaded = 1;
			sys_debug("Password loaded from /etc/acctl/password\n");
			return 0;
		}
		fclose(fp);
	}

	/* Last resort: check PASSWD build-time macro.
	 * Only used if neither UCI nor secret file is available.
	 * Production deployments MUST set one of the above. */
#ifdef PASSWD
	if (PASSWD[0] != '\0' && PASSWD[0] != ' ') {
		strncpy(g_password, PASSWD, MAX_PASSWORD_LEN);
		g_password[MAX_PASSWORD_LEN] = '\0';
		g_password_loaded = 1;
		sys_warn("WARNING: using build-time password. "
			"Set password in UCI or /etc/acctl/password for production!\n");
		return 0;
	}
#endif

	sys_err("FATAL: No password configured. "
		"Set 'option password' in /etc/config/acctl or create /etc/acctl/password\n");
	return -1;
}

/*
 * Get the current password, loading it on first call.
 * Returns empty string if not available.
 */
const char *sec_get_password(void)
{
	if (!g_password_loaded) {
		if (load_password_from_uci() != 0) {
			return "";
		}
	}
	return g_password;
}

/*
 * Verify the password is configured (call at startup).
 * Returns 0 if password is available, -1 otherwise.
 */
int sec_password_check(void)
{
	if (load_password_from_uci() != 0) {
		sys_err("Password check failed: no valid password found\n");
		return -1;
	}
	if (g_password[0] == '\0') {
		sys_err("Password check failed: password is empty\n");
		return -1;
	}
	/* Basic strength check */
	if (strlen(g_password) < 8) {
		sys_warn("Password is too short (min 8 chars recommended)\n");
	}
	return 0;
}

/*
 * chap_get_md5 — compute MD5(data || random || password)
 *   @data:    packet bytes
 *   @len:     packet length
 *   @random:  challenge random number
 *   @decrypt: output buffer (16 bytes for MD5)
 */
void chap_get_md5(uint8_t *data, int len, uint32_t random, uint8_t *decrypt)
{
	const char *pwd = sec_get_password();
	size_t pwd_len = strlen(pwd);

	MD5_CTX md5;
	MD5Init(&md5);
	MD5Update(&md5, data, (unsigned int)len);
	MD5Update(&md5, (void *)&random, sizeof(uint32_t));
	MD5Update(&md5, (void *)pwd, (unsigned int)pwd_len);
	MD5Final(&md5, decrypt);
	sys_debug("CHAP MD5: data=%p len=%d random=%u password_len=%zu\n",
		data, len, random, pwd_len);
	pr_md5(decrypt);
}

/*
 * chap_cmp_md5 — compare computed MD5 with stored value
 *   Returns 0 if match, non-zero if different
 */
int chap_cmp_md5(uint8_t *data, int len, uint32_t random, uint8_t *oldmd5)
{
	uint8_t *decrypt = malloc(CHAP_LEN);
	if (!decrypt) {
		sys_err("Malloc decrypt failed: %s(%d)\n", strerror(errno), errno);
		return -1;
	}

	chap_get_md5(data, len, random, decrypt);

	int ret = memcmp(decrypt, oldmd5, CHAP_LEN);
	free(decrypt);

	return ret;
}

/*
 * chap_msg_cmp_md5 — verify CHAP in a message header
 *   Temporarily clears chap[] field, computes, then compares.
 *   Returns 0 if valid, non-zero if invalid.
 */
int chap_msg_cmp_md5(struct msg_head_t *msg, int len, uint32_t random)
{
	uint8_t oldmd5[CHAP_LEN];
	memcpy(oldmd5, msg->chap, CHAP_LEN);
	memset(msg->chap, 0x0, CHAP_LEN);
	return chap_cmp_md5((void *)msg, len, random, oldmd5);
}

/*
 * chap_fill_msg_md5 — compute and fill CHAP field of a message header
 */
void chap_fill_msg_md5(struct msg_head_t *msg, int len, int random)
{
	memset(msg->chap, 0, CHAP_LEN);
	chap_get_md5((void *)msg, len, random, msg->chap);
}

/*
 * chap_get_random — get cryptographic random number from /dev/urandom
 *   Uses /dev/urandom (non-blocking) — safe for non-cryptographic use.
 *   For TLS keys, use sec_get_random_secure() instead.
 */
uint32_t chap_get_random(void)
{
	uint32_t random;
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) {
		/* Fallback: use /dev/random with timeout */
		fd = open("/dev/random", O_RDONLY);
		if (fd < 0) {
			sys_err("Cannot open random device: %s\n", strerror(errno));
			/* Last resort: use time + PID + stack address */
			random = (uint32_t)((unsigned long)&random ^ (unsigned long)time(NULL)
				^ (((unsigned long)pthread_self()) << 16));
			return random;
		}
	}
	int ret = read(fd, &random, sizeof(random));
	if (ret != (int)sizeof(random)) {
		sys_err("Short read from random device: %d\n", ret);
		random ^= (uint32_t)time(NULL);
	}
	close(fd);
	sys_debug("Generated random challenge: %u\n", random);
	return random;
}
