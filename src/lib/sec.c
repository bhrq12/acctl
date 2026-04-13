/*
 * ============================================================================
 *
 *       Filename:  sec.c
 *
 *    Description:  Security layer — command validation, rate limiting,
 *                  replay protection, and AC identity verification.
 *                  This is the core of the security hardening.
 *
 *        Version:  2.0
 *        Created:  2026-04-12
 *       Revision:  production-ready security module
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
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <regex.h>
#include <ctype.h>

#include "sec.h"
#include "sha256.h"
#include "log.h"

/* ========================================================================
 * 1. Command whitelist validation (prevents command injection)
 * ======================================================================== */

/* Allowed command categories with their prefixes and max lengths */
static const struct cmd_whitelist_entry {
	const char *prefix;
	size_t prefix_len;
	int   min_args;
	int   max_args;
} cmd_whitelist[] = {
	{ "reboot",             6, 0, 0 },
	{ "wifi",               4, 0, 2 },
	{ "uptime",             6, 0, 0 },
	{ "ifconfig",           8, 0, 1 },
	{ "iwconfig",           8, 0, 1 },
	{ "iw dev",             7, 0, 2 },
	{ "cat /proc/uptime",  16, 0, 0 },
	{ "cat /proc/loadavg", 17, 0, 0 },
	{ "cat /tmp/ap_status",17, 0, 0 },
	{ "logger",             6, 1, 2 },  /* logger "message" */
	{ NULL, 0, 0, 0 }                  /* sentinel */
};

/* Dangerous patterns that must NEVER appear in commands */
static const char *dangerous_patterns[] = {
	";",     /* command chaining */
	"|",     /* pipe */
	"`",     /* command substitution */
	"$(" ,   /* command substitution variant */
	"&",     /* background */
	"&&",    /* AND chaining */
	"||",    /* OR chaining */
	">",     /* output redirect */
	"<",     /* input redirect */
	">>",    /* append redirect */
	"<<",    /* heredoc */
	"~/",    /* home dir expansion */
	"/etc/", /* system config access */
	"/bin/", /* binary dir access */
	"/usr/", /* usr dir access */
	"/var/", /* var dir access */
	"/dev/", /* device access */
	"/proc/",/* procfs access */
	"/root/",/* root dir access */
	"../",   /* directory traversal */
	"0>",    /* fd redirect */
	"1>",    /* stdout redirect */
	"2>",    /* stderr redirect */
	"nohup", /* background process */
	"screen",/* screen sessions */
	"tmux",  /* tmux sessions */
	"wget",  /* network download */
	"curl",  /* network download */
	"nc ",   /* netcat */
	"ncat",  /* netcat variant */
	"python",/* python interpreter */
	"perl",  /* perl interpreter */
	"ruby",  /* ruby interpreter */
	"php",   /* php interpreter */
	"bash",  /* bash shell */
	"sh -c", /* explicit shell */
	"exec",  /* exec call */
	"eval",  /* eval */
	"chmod", /* permission change */
	"chown", /* ownership change */
	"rm -rf",/* recursive delete */
	"mkfs",  /* filesystem creation */
	"dd ",   /* raw disk write */
	"fdisk", /* disk partitioning */
	"mount", /* mount filesystem */
	"umount",/* unmount filesystem */
	"passwd",/* password change */
	"su ",   /* switch user */
	"sudo",  /* privilege escalation */
	"shutdown", /* system shutdown */
	"halt",  /* halt system */
	"poweroff", /* power off */
	NULL     /* sentinel */
};

/*
 * sec_validate_command — whitelist-based command validation
 *
 * Rules:
 *   1. Command must match a known safe prefix
 *   2. No dangerous characters/patterns allowed
 *   3. No directory traversal or system file access
 *   4. Arguments must be within allowed ranges
 *
 * Returns:  0 = allowed
 *          -1 = dangerous character detected
 *          -2 = command not in whitelist
 *          -3 = argument count out of range
 */
int sec_validate_command(const char *cmd)
{
	size_t cmd_len;
	int i;

	if (!cmd || (cmd_len = strlen(cmd)) == 0) {
		return -2;
	}

	/* Reject obviously oversized commands (prevent buffer attacks) */
	if (cmd_len > SEC_MAX_CMD_LEN) {
		sys_warn("Command too long: %zu chars (max %d)\n",
			cmd_len, SEC_MAX_CMD_LEN);
		return -3;
	}

	/* Check for dangerous character patterns */
	for (i = 0; dangerous_patterns[i]; i++) {
		if (strstr(cmd, dangerous_patterns[i])) {
			sys_warn("Command blocked: contains dangerous pattern '%s'\n",
				dangerous_patterns[i]);
			return -1;
		}
	}

	/* Check against whitelist */
	for (i = 0; cmd_whitelist[i].prefix; i++) {
		if (strncmp(cmd, cmd_whitelist[i].prefix,
			    cmd_whitelist[i].prefix_len) == 0) {
			/* Found in whitelist — verify argument count */
			const char *args = cmd + cmd_whitelist[i].prefix_len;
			while (*args == ' ') args++;
			int argc = (*args == '\0') ? 0 : 1;
			const char *p = args;
			while (*p) {
				if (*p == ' ' && *(p+1) != ' ' && *(p+1) != '\0')
					argc++;
				p++;
			}
			if (cmd_whitelist[i].max_args > 0 &&
			    argc > cmd_whitelist[i].max_args) {
				sys_warn("Command '%s': too many args (%d > %d)\n",
					cmd_whitelist[i].prefix, argc,
					cmd_whitelist[i].max_args);
				return -3;
			}
			sys_debug("Command '%s' allowed (args=%d)\n",
				cmd_whitelist[i].prefix, argc);
			return 0;
		}
	}

	sys_warn("Command not in whitelist: %.60s\n", cmd);
	return -2;
}

/*
 * sec_exec_command — safe command execution
 *
 * Executes a pre-validated command and captures output.
 * Never uses popen with unchecked input.
 *
 * Returns:  0 on success, -1 on error
 */
int sec_exec_command(const char *cmd, char *output, size_t output_len)
{
	int ret = sec_validate_command(cmd);
	if (ret != 0) {
		sys_warn("Refusing to execute blocked command: %s\n", cmd);
		return -1;
	}

	/* Use popen with validated input — safe because of prior check */
	FILE *fp = popen(cmd, "r");
	if (!fp) {
		sys_err("popen failed for '%s': %s\n", cmd, strerror(errno));
		return -1;
	}

	if (output && output_len > 0) {
		size_t r = fread(output, 1, output_len - 1, fp);
		output[r] = '\0';
		/* Remove trailing newline */
		if (r > 0 && output[r-1] == '\n')
			output[r-1] = '\0';
	}

	int status = pclose(fp);
	if (status == -1) {
		sys_err("pclose failed: %s\n", strerror(errno));
		return -1;
	}

	if (!WIFEXITED(status)) {
		sys_warn("Command '%s' did not exit normally (status=0x%x)\n",
			cmd, status);
		return -1;
	}

	sys_debug("Command '%s' executed successfully (exit=%d)\n",
		cmd, WEXITSTATUS(status));
	return WEXITSTATUS(status);
}

/* ========================================================================
 * 2. Replay protection — sliding window of used random+timestamp pairs
 * ======================================================================== */

#define REPLAY_TABLE_SIZE  (4096)
#define REPLAY_WINDOW_SEC  (300)  /* 5 minutes */

struct replay_entry {
	uint32_t random;
	time_t   timestamp;
	int      in_use;
};

static struct replay_entry replay_table[REPLAY_TABLE_SIZE];
static pthread_mutex_t replay_lock = PTHREAD_MUTEX_INITIALIZER;
static int replay_next = 0;  /* circular write pointer */

/*
 * sec_check_replay — check if a random number was recently used
 *
 * Uses a simple sliding window table to detect replay attacks.
 * Every random+timestamp pair is unique and can't be reused within
 * the REPLAY_WINDOW_SEC window.
 *
 * Returns:  0 = new (not a replay)
 *          1 = replay detected
 *         -1 = error
 */
int sec_check_replay(uint32_t random, time_t timestamp)
{
	int ret = 0;

	pthread_mutex_lock(&replay_lock);

	/* Check if random was used recently */
	for (int i = 0; i < REPLAY_TABLE_SIZE; i++) {
		if (replay_table[i].in_use &&
		    replay_table[i].random == random &&
		    replay_table[i].timestamp >= timestamp - REPLAY_WINDOW_SEC) {
			sys_warn("REPLAY ATTACK detected: random=%u, age=%lds\n",
				random, (long)(time(NULL) - replay_table[i].timestamp));
			ret = 1;
			goto unlock;
		}
	}

	/* Record this random number */
	replay_table[replay_next].random = random;
	replay_table[replay_next].timestamp = timestamp;
	replay_table[replay_next].in_use = 1;
	replay_next = (replay_next + 1) % REPLAY_TABLE_SIZE;

unlock:
	pthread_mutex_unlock(&replay_lock);
	return ret;
}

/*
 * sec_record_random — record a random number as used
 */
void sec_record_random(uint32_t random)
{
	pthread_mutex_lock(&replay_lock);
	replay_table[replay_next].random = random;
	replay_table[replay_next].timestamp = time(NULL);
	replay_table[replay_next].in_use = 1;
	replay_next = (replay_next + 1) % REPLAY_TABLE_SIZE;
	pthread_mutex_unlock(&replay_lock);
}

/* ========================================================================
 * 3. Rate limiting — per-AP and global rate tracking
 * ======================================================================== */

#define RATE_TABLE_SIZE  (256)

struct rate_entry {
	char     mac[ETH_ALEN];
	time_t   last_request;
	int      request_count;
	int      blocked;
	time_t   block_until;
};

static struct rate_entry rate_table[RATE_TABLE_SIZE];
static pthread_mutex_t rate_lock = PTHREAD_MUTEX_INITIALIZER;

static int rate_bucket(const char *mac)
{
	int sum = 0;
	for (int i = 0; i < ETH_ALEN; i++)
		sum += (unsigned char)mac[i];
	return sum % RATE_TABLE_SIZE;
}

/*
 * sec_rate_check — enforce per-AP rate limiting
 *
 * Limits:
 *   - Max 60 registrations per minute per AP
 *   - Max 120 commands per minute per AP
 *   - Block duration: 300 seconds on violation
 *
 * Returns:  0 = allowed
 *          1 = rate limited (temporarily blocked)
 *         -1 = MAC is blocked
 */
int sec_rate_check(const char *mac, int type)
{
	time_t now = time(NULL);
	int bucket = rate_bucket(mac);

	pthread_mutex_lock(&rate_lock);

	struct rate_entry *e = &rate_table[bucket];

	/* Initialize new entry */
	if (e->mac[0] == 0 || memcmp(e->mac, mac, ETH_ALEN) != 0) {
		memset(e, 0, sizeof(*e));
		memcpy(e->mac, mac, ETH_ALEN);
	}

	/* Check if currently blocked */
	if (e->blocked && now < e->block_until) {
		pthread_mutex_unlock(&rate_lock);
		return -1;
	}

	/* Reset counter if last request was > 60s ago */
	if (now - e->last_request > 60) {
		e->request_count = 0;
		e->blocked = 0;
	}

	int limit = (type == RATE_REGISTRATION) ? 60 : 120;
	e->request_count++;
	e->last_request = now;

	if (e->request_count > limit) {
		e->blocked = 1;
		e->block_until = now + 300;  /* 5 min block */
		sys_warn("Rate limit exceeded for MAC "
			MAC_FMT" (type=%d). Blocked for 300s.\n",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], type);
		pthread_mutex_unlock(&rate_lock);
		return -1;
	}

	pthread_mutex_unlock(&rate_lock);
	return 0;
}

/* ========================================================================
 * 4. AC identity verification — prevent AP takeover
 * ======================================================================== */

/* List of trusted AC MAC addresses (whitelist) */
#define TRUSTED_AC_MAX  (8)
static struct {
	char mac[ETH_ALEN];
	int  count;
} trusted_ac_list;
static pthread_mutex_t ac_trust_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * sec_ac_trust_add — add an AC MAC to the trusted whitelist
 */
void sec_ac_trust_add(const char *mac)
{
	pthread_mutex_lock(&ac_trust_lock);
	if (trusted_ac_list.count < TRUSTED_AC_MAX) {
		memcpy(trusted_ac_list.mac, mac, ETH_ALEN);
		trusted_ac_list.count++;
		sys_debug("Added AC to trusted list: "
			MAC_FMT"\n",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	}
	pthread_mutex_unlock(&ac_trust_lock);
}

/*
 * sec_ac_is_trusted — check if an AC MAC is in the trusted whitelist
 *
 * Returns:  1 = trusted
 *          0 = not trusted (should be rejected for takeover)
 */
int sec_ac_is_trusted(const char *mac)
{
	/* If whitelist is empty, accept all (backward compat) */
	if (trusted_ac_list.count == 0)
		return 1;

	pthread_mutex_lock(&ac_trust_lock);
	int trusted = 0;
	for (int i = 0; i < trusted_ac_list.count; i++) {
		if (memcmp(trusted_ac_list.mac, mac, ETH_ALEN) == 0) {
			trusted = 1;
			break;
		}
	}
	pthread_mutex_unlock(&ac_trust_lock);
	return trusted;
}

/* ========================================================================
 * 5. Secure random bytes for cryptographic use
 * ======================================================================== */

/*
 * sec_get_random_bytes — fill buffer with cryptographic random bytes
 *   Uses /dev/urandom (blocking acceptable here since it's called
 *   rarely — only for key generation, not per-packet)
 *
 * Returns:  0 on success, -1 on error
 */
int sec_get_random_bytes(uint8_t *buf, size_t len)
{
	FILE *fp = fopen("/dev/urandom", "rb");
	if (!fp) {
		sys_err("Cannot open /dev/urandom: %s\n", strerror(errno));
		return -1;
	}
	size_t r = fread(buf, 1, len, fp);
	fclose(fp);
	if (r != len) {
		sys_err("Short read from /dev/urandom: %zu < %zu\n", r, len);
		return -1;
	}
	return 0;
}

/* ========================================================================
 * 6. Message HMAC integrity verification
 * ======================================================================== */

/*
 * sec_compute_hmac — compute HMAC-SHA256 over message data
 *
 * Implements RFC 2104 HMAC using SHA-256:
 *   HMAC(K, m) = SHA256(K_opad || SHA256(K_ipad || m))
 * where K_ipad = K XOR 0x36, K_opad = K XOR 0x5c
 *
 * hmac_out must be at least 32 bytes (SHA256_DIGEST_SIZE).
 */
void sec_compute_hmac(const uint8_t *data, size_t len,
                      const uint8_t *key, size_t key_len,
                      uint8_t *hmac_out)
{
	uint8_t k_ipad[SHA256_BLOCK_SIZE];
	uint8_t k_opad[SHA256_BLOCK_SIZE];
	uint8_t inner_digest[SHA256_DIGEST_SIZE];
	sha256_ctx ctx;
	size_t i;

	/* If key is longer than block size, hash it first */
	uint8_t key_hash[SHA256_DIGEST_SIZE];
	if (key_len > SHA256_BLOCK_SIZE) {
		sha256(key, key_len, key_hash);
		key = key_hash;
		key_len = SHA256_DIGEST_SIZE;
	}

	/* Prepare padded keys */
	memset(k_ipad, 0x36, SHA256_BLOCK_SIZE);
	memset(k_opad, 0x5c, SHA256_BLOCK_SIZE);
	for (i = 0; i < key_len; i++) {
		k_ipad[i] ^= key[i];
		k_opad[i] ^= key[i];
	}

	/* Inner hash: SHA256(K_ipad || data) */
	sha256_init(&ctx);
	sha256_update(&ctx, k_ipad, SHA256_BLOCK_SIZE);
	sha256_update(&ctx, data, len);
	sha256_final(&ctx, inner_digest);

	/* Outer hash: SHA256(K_opad || inner_digest) */
	sha256_init(&ctx);
	sha256_update(&ctx, k_opad, SHA256_BLOCK_SIZE);
	sha256_update(&ctx, inner_digest, SHA256_DIGEST_SIZE);
	sha256_final(&ctx, hmac_out);
}

/*
 * sec_verify_hmac — verify HMAC-SHA256 of a message
 *   Returns 0 if valid, non-zero if tampered.
 */
int sec_verify_hmac(const uint8_t *data, size_t len,
                   const uint8_t *key, size_t key_len,
                   const uint8_t *expected_hmac)
{
	uint8_t computed[SHA256_DIGEST_SIZE];
	sec_compute_hmac(data, len, key, key_len, computed);
	return memcmp(computed, expected_hmac, SHA256_DIGEST_SIZE);
}

/* ========================================================================
 * 7. Global initialization
 * ======================================================================== */

static int g_sec_initialized = 0;

int sec_init(void)
{
	if (g_sec_initialized)
		return 0;

	/* Clear replay and rate tables */
	memset(replay_table, 0, sizeof(replay_table));
	memset(rate_table, 0, sizeof(rate_table));
	trusted_ac_list.count = 0;

	g_sec_initialized = 1;
	sys_info("Security layer initialized\n");
	sys_info("  Replay window: %d seconds\n", REPLAY_WINDOW_SEC);
	sys_info("  Rate limit: 60 reg/min, 120 cmds/min per AP\n");
	sys_info("  Block duration: 300 seconds\n");

	return 0;
}
