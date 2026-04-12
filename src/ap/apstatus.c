/*
 * ============================================================================
 *
 *       Filename:  apstatus.c
 *
 *    Description:  AP system status collection.
 *                  Gathers real system information from /proc and UCI.
 *
 *        Version:  2.0
 *        Created:  2026-04-12
 *       Compiler:  gcc
 *
 * ============================================================================
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/statvfs.h>

#include "apstatus.h"
#include "log.h"

static struct apstatus_t cached_status;
static time_t cache_time = 0;
#define CACHE_TTL  5  /* seconds */

/* ========================================================================
 * Read a /proc file into a buffer
 * ======================================================================== */

static int read_proc(const char *path, char *buf, size_t buflen)
{
	FILE *fp = fopen(path, "r");
	if (!fp)
		return -1;

	size_t r = fread(buf, 1, buflen - 1, fp);
	buf[r] = '\0';
	fclose(fp);

	/* Trim newline */
	while (r > 0 && (buf[r-1] == '\n' || buf[r-1] == '\r'))
		buf[--r] = '\0';

	return (int)r;
}

/* ========================================================================
 * Get WiFi interface name from UCI
 * ======================================================================== */

/* ========================================================================
 * Get WiFi interface name (radio device name, e.g. "radio0")
 * ======================================================================== */

static void get_wifi_iface(char *buf, size_t buflen)
{
	FILE *fp = popen(
		"uci get wireless.@wifi-iface[0].device 2>/dev/null | tr -d '\\n\\r' || echo wifi0",
		"r");
	if (!fp) {
		strncpy(buf, "wifi0", buflen - 1);
		buf[buflen - 1] = '\0';
		return;
	}
	if (fgets(buf, (int)buflen, fp)) {
		size_t len = strlen(buf);
		while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
			buf[--len] = '\0';
		if (buf[0] == '\0')
			strncpy(buf, "wifi0", buflen - 1);
	}
	pclose(fp);
}

/* ========================================================================
 * Get current SSID (from the first wifi-iface section)
 * ======================================================================== */

static void get_ssid(char *ssid_buf, size_t ssid_len)
{
	FILE *fp = popen(
		"uci get wireless.@wifi-iface[0].ssid 2>/dev/null | tr -d '\\n\\r' || echo ''",
		"r");
	if (!fp) {
		ssid_buf[0] = '\0';
		return;
	}
	if (fgets(ssid_buf, (int)ssid_len, fp)) {
		size_t len = strlen(ssid_buf);
		while (len > 0 && (ssid_buf[len-1] == '\n' || ssid_buf[len-1] == '\r'))
			ssid_buf[--len] = '\0';
	}
	pclose(fp);
}

/* ========================================================================
 * Get number of associated clients
 * ======================================================================== */

/* ========================================================================
 * Get uptime
 * ======================================================================== */

static unsigned long get_uptime_sec(void)
{
	char buf[64];
	if (read_proc("/proc/uptime", buf, sizeof(buf)) > 0) {
		return (unsigned long)atof(buf);
	}
	return 0;
}

/* ========================================================================
 * Get free memory (KB)
 * ======================================================================== */

static unsigned long get_memfree_kb(void)
{
	char buf[256];
	if (read_proc("/proc/meminfo", buf, sizeof(buf)) > 0) {
		char *p = strstr(buf, "MemFree:");
		if (p) {
			unsigned long kb;
			if (sscanf(p, "MemFree: %lu", &kb) == 1)
				return kb;
		}
	}
	return 0;
}

/* ========================================================================
 * Get CPU usage (approximate via /proc/stat)
 * ======================================================================== */

static unsigned int get_cpu_percent(void)
{
	static unsigned long prev_total = 0;
	static unsigned long prev_idle = 0;
	char buf[256];
	unsigned long user, nice, system, idle, iowait, irq, softirq;
	unsigned long total, total_d, idle_d;
	unsigned int pct;

	if (read_proc("/proc/stat", buf, sizeof(buf)) <= 0)
		return 0;

	if (sscanf(buf, "cpu  %lu %lu %lu %lu %lu %lu %lu",
		&user, &nice, &system, &idle, &iowait, &irq, &softirq) != 7)
		return 0;

	total = user + nice + system + idle + iowait + irq + softirq;
	idle = idle + iowait;

	total_d = total - prev_total;
	idle_d = idle - prev_idle;

	prev_total = total;
	prev_idle = idle;

	if (total_d == 0)
		return 0;

	pct = (unsigned int)((total_d - idle_d) * 100 / total_d);
	if (pct > 100) pct = 100;

	return pct;
}

/* ========================================================================
 * Get cached or fresh status
 * ======================================================================== */

struct apstatus_t *get_apstatus(void)
{
	time_t now = time(NULL);

	if (now - cache_time < CACHE_TTL)
		return &cached_status;

	memset(&cached_status, 0, sizeof(cached_status));

	/* Get SSID first */
	get_ssid(cached_status.ssid0.ssid, sizeof(cached_status.ssid0.ssid));

	/* Get radio device name (e.g. "radio0") */
	char iface[32];
	get_wifi_iface(iface, sizeof(iface));

	/* Signal strength from iw — use the correct interface name */
	if (iface[0] != '\0') {
		char cmd[128];
		char sig_buf[32];
		snprintf(cmd, sizeof(cmd),
			"iw dev %s link 2>/dev/null | grep 'signal:' | awk '{print $2}'",
			iface);
		FILE *fp = popen(cmd, "r");
		if (fp) {
			if (fgets(sig_buf, sizeof(sig_buf), fp)) {
				cached_status.ssid0.power = atoi(sig_buf);
			}
			pclose(fp);
		}

		/* Associated users: count STAs connected to this interface */
		char cmd2[128];
		char sta_buf[32];
		snprintf(cmd2, sizeof(cmd2),
			"iw dev %s station dump 2>/dev/null | grep -c 'Station' || echo 0",
			iface);
		fp = popen(cmd2, "r");
		if (fp) {
			if (fgets(sta_buf, sizeof(sta_buf), fp)) {
				cached_status.ssidnum = atoi(sta_buf);
			}
			pclose(fp);
		} else {
			cached_status.ssidnum = 0;
		}
	}

	/* Fallback SSID if none found */
	if (cached_status.ssid0.ssid[0] == '\0') {
		strncpy(cached_status.ssid0.ssid, "OpenWrt-AP",
			sizeof(cached_status.ssid0.ssid) - 1);
	}

	cache_time = now;
	return &cached_status;
}

/*
 * get_uptime — return system uptime in seconds
 */
unsigned long get_uptime(void)
{
	return get_uptime_sec();
}

/*
 * get_memfree — return free memory in KB
 */
unsigned long get_memfree(void)
{
	return get_memfree_kb();
}

/*
 * get_cpu_usage — return CPU usage percentage
 */
unsigned int get_cpu_usage(void)
{
	return get_cpu_percent();
}
