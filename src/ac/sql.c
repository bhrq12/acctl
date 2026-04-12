/*
 * ============================================================================
 *
 *       Filename:  sql.c
 *
 *    Description:  SQLite database operations for AC Controller.
 *                  Extended from v1.1 to support:
 *                    - AP groups and tagging
 *                    - Alarm/event logging
 *                    - Firmware repository
 *                    - Upgrade tracking
 *                    - Audit logging
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
#include <sqlite3.h>
#include <time.h>

#include "sql.h"
#include "log.h"

SQL sql = NULL;
struct tbl_col_t tables;

const char *json_attrs[] = {
	"ip_start",
	"ip_end",
	"ip_mask",
	NULL
};

/* ========================================================================
 * Initialization
 * ======================================================================== */

void sql_close(SQL db)
{
	if (db && db != sql) {
		sqlite3_close(db);
	} else if (sql) {
		sqlite3_close(sql);
		sql = NULL;
	}
}

void _sql_tbl_col(SQL db, const char *table, struct col_name_t *head, int *col_num)
{
	char sql_cmd[256];
	sqlite3_stmt *stmt;
	int rc;

	snprintf(sql_cmd, sizeof(sql_cmd), "PRAGMA table_info(%s)", table);
	rc = sqlite3_prepare_v2(db, sql_cmd, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		goto err;

	*col_num = 0;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *col_name = (const char *)sqlite3_column_text(stmt, 1);
		if (col_name && *col_num < COLMAX - 1) {
			strncpy(head[*col_num].name, col_name, COLMAX - 1);
			head[*col_num].name[COLMAX - 1] = '\0';
			(*col_num)++;
		}
	}
	sqlite3_finalize(stmt);
	return;

err:
	sys_err("Failed to get table info for '%s': %s\n",
		table, sqlite3_errmsg(db));
}

void sql_tbl_col(SQL db)
{
	_sql_tbl_col(db, RESOURCE, tables.res.head, &tables.res.col_num);
}

int sql_init(SQL db)
{
	int rc;
	char *err_msg = NULL;
	char sql_cmd[512];

	if (db)
		sql = db;

	printf("SQLite version: %s\n", sqlite3_libversion());
	printf("SQLite compile flags: %s\n", sqlite3_compileoption_get(0));

	rc = sqlite3_open(DBNAME, &sql);
	if (rc != SQLITE_OK) {
		sys_err("Cannot open database '%s': %s\n",
			DBNAME, sqlite3_errmsg(sql));
		return -1;
	}

	/* Enable WAL mode for better concurrent read/write performance */
	rc = sqlite3_exec(sql, "PRAGMA journal_mode=WAL", NULL, NULL, &err_msg);
	if (rc != SQLITE_OK) {
		sys_warn("WAL mode not available: %s\n", err_msg);
		sqlite3_free(err_msg);
	}

	/* Set busy timeout */
	sqlite3_busy_timeout(sql, 5000);

	/* ---- Table: resource ---- */
#define RUN_SQL(stmt) do { \
	rc = sqlite3_exec(sql, stmt, NULL, NULL, &err_msg); \
	if (rc != SQLITE_OK) { \
		sys_err("SQL error: %s\n  in: %s\n", err_msg, stmt); \
		sqlite3_free(err_msg); \
		return -1; \
	} \
} while(0)

	RUN_SQL(
		"CREATE TABLE IF NOT EXISTS resource ("
		"  ip_start TEXT,"
		"  ip_end TEXT,"
		"  ip_mask TEXT"
		");"
	);

	/* ---- Table: node (AP device info) ---- */
	RUN_SQL(
		"CREATE TABLE IF NOT EXISTS node ("
		"  mac TEXT PRIMARY KEY,"
		"  hostname TEXT,"
		"  registered_ac TEXT,"
		"  time_first TEXT,"
		"  time TEXT,"
		"  latitude TEXT,"
		"  longitude TEXT,"
		"  uptime TEXT,"
		"  memfree TEXT,"
		"  cpu TEXT,"
		"  device_down INTEGER DEFAULT 0,"
		"  wan_iface TEXT,"
		"  wan_ip TEXT,"
		"  wan_mac TEXT,"
		"  wan_gateway TEXT,"
		"  wifi_iface TEXT,"
		"  wifi_ip TEXT,"
		"  wifi_mac TEXT,"
		"  wifi_ssid TEXT,"
		"  wifi_encryption TEXT,"
		"  wifi_key TEXT,"
		"  wifi_channel_mode TEXT,"
		"  wifi_channel TEXT,"
		"  wifi_signal TEXT,"
		"  lan_iface TEXT,"
		"  lan_mac TEXT,"
		"  lan_ip TEXT,"
		"  wan_bup TEXT,"
		"  wan_bup_sum TEXT,"
		"  wan_bdown TEXT,"
		"  wan_bdown_sum TEXT,"
		"  firmware TEXT,"
		"  firmware_revision TEXT,"
		"  online_user_num INTEGER DEFAULT 0,"
		"  group_id INTEGER DEFAULT 0,"
		"  config_version INTEGER DEFAULT 0,"
		"  last_config_time TEXT,"
		"  last_seen INTEGER DEFAULT 0,"
		"  tags TEXT DEFAULT '[]'"
		");"
	);

	/* ---- Table: node_default (AP default config templates) ---- */
	RUN_SQL(
		"CREATE TABLE IF NOT EXISTS node_default ("
		"  profile TEXT PRIMARY KEY,"
		"  device_name TEXT,"
		"  wifi_ssid TEXT NOT NULL,"
		"  wifi_encryption TEXT NOT NULL,"
		"  wifi_key TEXT NOT NULL,"
		"  wifi_channel_mode TEXT NOT NULL,"
		"  wifi_channel TEXT,"
		"  wifi_signal TEXT"
		");"
	);

	/* ---- Table: node_setting (AP pre-configuration) ---- */
	RUN_SQL(
		"CREATE TABLE IF NOT EXISTS node_setting ("
		"  pre_device_name TEXT,"
		"  pre_device_mac TEXT UNIQUE,"
		"  pre_device_description TEXT,"
		"  device_latitude TEXT,"
		"  device_longitude TEXT,"
		"  wan_ip TEXT,"
		"  wan_mac TEXT UNIQUE,"
		"  wifi_ip TEXT,"
		"  wifi_ssid TEXT,"
		"  wifi_encryption TEXT,"
		"  wifi_key TEXT,"
		"  wifi_channel_mode TEXT,"
		"  wifi_channel TEXT,"
		"  wifi_signal TEXT"
		");"
	);

	/* ---- Table: ap_group (AP grouping) ---- */
	RUN_SQL(
		"CREATE TABLE IF NOT EXISTS ap_group ("
		"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  name TEXT NOT NULL UNIQUE,"
		"  description TEXT,"
		"  update_policy TEXT DEFAULT 'manual',"
		"  config_profile TEXT,"
		"  created_at TEXT DEFAULT (datetime('now'))"
		");"
	);

	/* ---- Table: alarm_event ---- */
	RUN_SQL(
		"CREATE TABLE IF NOT EXISTS alarm_event ("
		"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  ap_mac TEXT,"
		"  alarm_rule_id INTEGER,"
		"  level INTEGER,"
		"  message TEXT,"
		"  raw_data TEXT,"
		"  created_at TEXT DEFAULT (datetime('now')),"
		"  acknowledged INTEGER DEFAULT 0,"
		"  acknowledged_by TEXT,"
		"  acknowledged_at TEXT,"
		"  resolved_at TEXT"
		");"
	);
	RUN_SQL(
		"CREATE INDEX IF NOT EXISTS idx_alarm_created "
		"ON alarm_event(created_at DESC);"
	);
	RUN_SQL(
		"CREATE INDEX IF NOT EXISTS idx_alarm_ap_mac "
		"ON alarm_event(ap_mac);"
	);

	/* ---- Table: firmware ---- */
	RUN_SQL(
		"CREATE TABLE IF NOT EXISTS firmware ("
		"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  version TEXT NOT NULL UNIQUE,"
		"  filename TEXT,"
		"  file_size INTEGER,"
		"  sha256 TEXT,"
		"  signature TEXT,"
		"  uploaded_at TEXT DEFAULT (datetime('now')),"
		"  uploaded_by TEXT,"
		"  notes TEXT,"
		"  min_hw_version TEXT"
		");"
	);

	/* ---- Table: upgrade_log ---- */
	RUN_SQL(
		"CREATE TABLE IF NOT EXISTS upgrade_log ("
		"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  ap_mac TEXT,"
		"  from_version TEXT,"
		"  to_version TEXT,"
		"  status TEXT,"
		"  started_at TEXT DEFAULT (datetime('now')),"
		"  finished_at TEXT,"
		"  error_message TEXT"
		");"
	);

	/* ---- Table: audit_log ---- */
	RUN_SQL(
		"CREATE TABLE IF NOT EXISTS audit_log ("
		"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  user TEXT,"
		"  action TEXT,"
		"  resource_type TEXT,"
		"  resource_id TEXT,"
		"  old_value TEXT,"
		"  new_value TEXT,"
		"  ip_address TEXT,"
		"  created_at TEXT DEFAULT (datetime('now'))"
		");"
	);
	RUN_SQL(
		"CREATE INDEX IF NOT EXISTS idx_audit_created "
		"ON audit_log(created_at DESC);"
	);

#undef RUN_SQL

	/* Initialize column name cache */
	tables.res.head = malloc(sizeof(struct col_name_t) * COLMAX);
	tables.res.col_num = 0;
	sql_tbl_col(sql);

	sys_info("Database initialized successfully: %s\n", DBNAME);
	return 0;
}

/* ========================================================================
 * Resource query (IP pool config)
 * ======================================================================== */

int sql_query_res(SQL db, char *buffer, int len)
{
	sqlite3_stmt *stmt;
	int rc;

	rc = sqlite3_prepare_v2(db ? db : sql, GETRES, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		pr_sqlerr();
		return -1;
	}

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		return -1;
	}

	JSON_ENCODE_START(buffer, len);
	for (int i = 0; i < tables.res.col_num && len > 2; i++) {
		const char *value = (const char *)sqlite3_column_text(stmt, i);
		if (!value)
			value = SQL_NULL;
		JSON_ENCODE(buffer, len, tables.res.head[i].name, value);
	}
	JSON_ENCODE_END(buffer, len);

	sqlite3_finalize(stmt);
	return 0;
}

/* ========================================================================
 * AP operations
 * ======================================================================== */

static int _sql_exec(const char *sql_stmt, ...)
{
	char *err_msg = NULL;
	int rc = sqlite3_exec(sql, sql_stmt, NULL, NULL, &err_msg);
	if (rc != SQLITE_OK) {
		sys_err("SQL exec failed: %s\n  stmt: %s\n",
			err_msg, sql_stmt);
		sqlite3_free(err_msg);
		return -1;
	}
	return 0;
}

int sql_ap_upsert(const char *mac, const char *hostname,
	const char *wan_ip, const char *wifi_ssid,
	const char *firmware, int online_users, const char *extra_json)
{
	char sql_stmt[1024];
	char timestamp[64];
	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

	snprintf(sql_stmt, sizeof(sql_stmt),
		"INSERT INTO node (mac, hostname, wan_ip, wifi_ssid, firmware, "
		"online_user_num, last_seen, time) "
		"VALUES ('%s', '%s', '%s', '%s', '%s', %d, %lu, '%s') "
		"ON CONFLICT(mac) DO UPDATE SET "
		"  hostname=COALESCE(excluded.hostname,hostname),"
		"  wan_ip=COALESCE(excluded.wan_ip,wan_ip),"
		"  wifi_ssid=COALESCE(excluded.wifi_ssid,wifi_ssid),"
		"  firmware=COALESCE(excluded.firmware,firmware),"
		"  online_user_num=COALESCE(excluded.online_user_num,online_user_num),"
		"  last_seen=%lu,"
		"  device_down=0,"
		"  time='%s'",
		mac, hostname ? hostname : "", wan_ip ? wan_ip : "",
		wifi_ssid ? wifi_ssid : "", firmware ? firmware : "",
		online_users, (unsigned long)now, timestamp,
		(unsigned long)now, timestamp);

	return _sql_exec(sql_stmt);
}

int sql_ap_update_field(const char *mac, const char *field, const char *value)
{
	char sql_stmt[512];
	snprintf(sql_stmt, sizeof(sql_stmt),
		"UPDATE node SET %s='%s' WHERE mac='%s'",
		field, value, mac);
	return _sql_exec(sql_stmt);
}

int sql_ap_get_field(const char *mac, const char *field, char *out, int outlen)
{
	char sql_stmt[256];
	sqlite3_stmt *stmt;
	int rc;

	snprintf(sql_stmt, sizeof(sql_stmt),
		"SELECT %s FROM node WHERE mac='%s'", field, mac);

	rc = sqlite3_prepare_v2(sql, sql_stmt, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) {
		const char *val = (const char *)sqlite3_column_text(stmt, 0);
		if (val && out)
			strncpy(out, val, outlen - 1);
		out[outlen - 1] = '\0';
		rc = 0;
	} else {
		rc = -1;
	}
	sqlite3_finalize(stmt);
	return rc;
}

int sql_ap_set_offline(const char *mac)
{
	char sql_stmt[256];
	snprintf(sql_stmt, sizeof(sql_stmt),
		"UPDATE node SET device_down=1, last_seen=%lu "
		"WHERE mac='%s'",
		(unsigned long)time(NULL), mac);
	return _sql_exec(sql_stmt);
}

/* ========================================================================
 * AP Group operations
 * ======================================================================== */

int sql_group_create(const char *name, const char *description)
{
	char sql_stmt[512];
	snprintf(sql_stmt, sizeof(sql_stmt),
		"INSERT INTO ap_group (name, description) VALUES ('%s', '%s')",
		name, description ? description : "");
	return _sql_exec(sql_stmt);
}

int sql_group_delete(int group_id)
{
	char sql_stmt[128];
	snprintf(sql_stmt, sizeof(sql_stmt),
		"DELETE FROM ap_group WHERE id=%d", group_id);
	return _sql_exec(sql_stmt);
}

int sql_group_list(char *json_buf, int buflen)
{
	sqlite3_stmt *stmt;
	int rc, n = 0;
	char *p = json_buf;
	int space = buflen;

	rc = sqlite3_prepare_v2(sql,
		"SELECT id,name,description,update_policy FROM ap_group "
		"ORDER BY id", -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	n = snprintf(p, space, "{\"groups\":[");
	if (n < 0 || n >= space) { sqlite3_finalize(stmt); return -1; }
	p += n; space -= n;

	int first = 1;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *name = (const char *)sqlite3_column_text(stmt, 1);
		const char *desc = (const char *)sqlite3_column_text(stmt, 2);
		const char *policy = (const char *)sqlite3_column_text(stmt, 3);
		int id = sqlite3_column_int(stmt, 0);

		int w = snprintf(p, space,
			"%s{\"id\":%d,\"name\":\"%s\",\"description\":\"%s\","
			"\"policy\":\"%s\"}",
			first ? "" : ",",
			id, name ? name : "", desc ? desc : "",
			policy ? policy : "manual");
		if (w < 0 || w >= space) { sqlite3_finalize(stmt); return -1; }
		p += w; space -= w; first = 0;
	}
	sqlite3_finalize(stmt);

	n = snprintf(p, space, "]}");
	if (n < 0 || n >= space) return -1;
	p += n;

	return (int)(p - json_buf);
}

int sql_group_add_ap(const char *mac, int group_id)
{
	char sql_stmt[256];
	snprintf(sql_stmt, sizeof(sql_stmt),
		"UPDATE node SET group_id=%d WHERE mac='%s'",
		group_id, mac);
	return _sql_exec(sql_stmt);
}

int sql_group_remove_ap(const char *mac, int group_id)
{
	(void)group_id;
	char sql_stmt[256];
	snprintf(sql_stmt, sizeof(sql_stmt),
		"UPDATE node SET group_id=0 WHERE mac='%s'", mac);
	return _sql_exec(sql_stmt);
}

/* ========================================================================
 * Alarm operations
 * ======================================================================== */

int sql_alarm_insert(int level, const char *ap_mac, const char *message,
	const char *raw_data)
{
	char sql_stmt[1024];
	snprintf(sql_stmt, sizeof(sql_stmt),
		"INSERT INTO alarm_event (ap_mac, level, message, raw_data) "
		"VALUES ('%s', %d, '%s', '%s')",
		ap_mac ? ap_mac : "unknown",
		level,
		message ? message : "",
		raw_data ? raw_data : "");
	return _sql_exec(sql_stmt);
}

int sql_alarm_ack(int alarm_id, const char *acked_by)
{
	char sql_stmt[256];
	char timestamp[64];
	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

	snprintf(sql_stmt, sizeof(sql_stmt),
		"UPDATE alarm_event SET acknowledged=1,"
		"acknowledged_by='%s', acknowledged_at='%s' "
		"WHERE id=%d",
		acked_by ? acked_by : "system", timestamp, alarm_id);
	return _sql_exec(sql_stmt);
}

int sql_alarm_list(char *json_buf, int buflen, int limit)
{
	char sql_stmt[256];
	sqlite3_stmt *stmt;
	int rc, n = 0;
	char *p = json_buf;
	int space = buflen;

	snprintf(sql_stmt, sizeof(sql_stmt),
		"SELECT id,ap_mac,level,message,acknowledged,created_at "
		"FROM alarm_event ORDER BY created_at DESC LIMIT %d", limit);

	rc = sqlite3_prepare_v2(sql, sql_stmt, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	n = snprintf(p, space, "{\"alarms\":[");
	if (n < 0 || n >= space) { sqlite3_finalize(stmt); return -1; }
	p += n; space -= n;

	int first = 1;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *msg = (const char *)sqlite3_column_text(stmt, 3);
		const char *mac = (const char *)sqlite3_column_text(stmt, 1);
		const char *ts = (const char *)sqlite3_column_text(stmt, 5);
		int id = sqlite3_column_int(stmt, 0);
		int level = sqlite3_column_int(stmt, 2);
		int ack = sqlite3_column_int(stmt, 4);

		const char *level_str =
			(level == 0) ? "info" :
			(level == 1) ? "warn" :
			(level == 2) ? "error" :
			(level == 3) ? "critical" : "unknown";

		int w = snprintf(p, space,
			"%s{\"id\":%d,\"mac\":\"%s\",\"level\":\"%s\","
			"\"message\":\"%s\",\"ack\":%d,\"ts\":\"%s\"}",
			first ? "" : ",",
			id, mac ? mac : "", level_str,
			msg ? msg : "", ack, ts ? ts : "");
		if (w < 0 || w >= space) { sqlite3_finalize(stmt); return -1; }
		p += w; space -= w; first = 0;
	}
	sqlite3_finalize(stmt);

	n = snprintf(p, space, "]}");
	if (n < 0 || n >= space) return -1;

	return (int)(p - json_buf);
}

int sql_alarm_count_by_level(void)
{
	sqlite3_stmt *stmt;
	int total = 0;
	int rc = sqlite3_prepare_v2(sql,
		"SELECT COUNT(*) FROM alarm_event WHERE acknowledged=0",
		-1, &stmt, NULL);
	if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW)
		total = sqlite3_column_int(stmt, 0);
	sqlite3_finalize(stmt);
	return total;
}

/* ========================================================================
 * Firmware operations
 * ======================================================================== */

int sql_firmware_insert(const char *version, const char *filename,
	uint32_t file_size, const char *sha256)
{
	char sql_stmt[1024];
	snprintf(sql_stmt, sizeof(sql_stmt),
		"INSERT INTO firmware (version, filename, file_size, sha256) "
		"VALUES ('%s', '%s', %u, '%s')",
		version, filename ? filename : "", file_size,
		sha256 ? sha256 : "");
	return _sql_exec(sql_stmt);
}

int sql_firmware_list(char *json_buf, int buflen)
{
	sqlite3_stmt *stmt;
	int rc, n = 0;
	char *p = json_buf;
	int space = buflen;

	rc = sqlite3_prepare_v2(sql,
		"SELECT version,filename,file_size,sha256,uploaded_at "
		"FROM firmware ORDER BY uploaded_at DESC", -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	n = snprintf(p, space, "{\"firmwares\":[");
	if (n < 0 || n >= space) { sqlite3_finalize(stmt); return -1; }
	p += n; space -= n;

	int first = 1;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *ver = (const char *)sqlite3_column_text(stmt, 0);
		const char *fn = (const char *)sqlite3_column_text(stmt, 1);
		int sz = sqlite3_column_int(stmt, 2);
		const char *ts = (const char *)sqlite3_column_text(stmt, 4);

		int w = snprintf(p, space,
			"%s{\"version\":\"%s\",\"filename\":\"%s\","
			"\"size\":%d,\"uploaded_at\":\"%s\"}",
			first ? "" : ",",
			ver ? ver : "", fn ? fn : "", sz,
			ts ? ts : "");
		if (w < 0 || w >= space) { sqlite3_finalize(stmt); return -1; }
		p += w; space -= w; first = 0;
	}
	sqlite3_finalize(stmt);

	n = snprintf(p, space, "]}");
	if (n < 0 || n >= space) return -1;
	return (int)(p - json_buf);
}

int sql_firmware_getlatest(char *version_out, int version_len)
{
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(sql,
		"SELECT version FROM firmware ORDER BY uploaded_at DESC LIMIT 1",
		-1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *ver = (const char *)sqlite3_column_text(stmt, 0);
		if (ver && version_out)
			strncpy(version_out, ver, version_len - 1);
		version_out[version_len - 1] = '\0';
		rc = 0;
	} else {
		rc = -1;
	}
	sqlite3_finalize(stmt);
	return rc;
}

/* ========================================================================
 * Upgrade log
 * ======================================================================== */

int sql_upgrade_start(const char *ap_mac, const char *from_ver, const char *to_ver)
{
	char sql_stmt[512];
	snprintf(sql_stmt, sizeof(sql_stmt),
		"INSERT INTO upgrade_log (ap_mac, from_version, to_version, status) "
		"VALUES ('%s', '%s', '%s', 'pending')",
		ap_mac, from_ver ? from_ver : "", to_ver ? to_ver : "");
	return _sql_exec(sql_stmt);
}

int sql_upgrade_finish(const char *ap_mac, const char *status, const char *error_msg)
{
	char sql_stmt[512];
	char timestamp[64];
	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

	snprintf(sql_stmt, sizeof(sql_stmt),
		"UPDATE upgrade_log SET status='%s',finished_at='%s',"
		"error_message='%s' "
		"WHERE ap_mac='%s' AND status='pending'",
		status ? status : "unknown",
		timestamp,
		error_msg ? error_msg : "",
		ap_mac);
	return _sql_exec(sql_stmt);
}

int sql_upgrade_progress(const char *ap_mac, int *status_out,
	char *from_ver, int from_len, char *to_ver, int to_len,
	char *error_msg, int err_len)
{
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(sql,
		"SELECT status,from_version,to_version,error_message "
		"FROM upgrade_log WHERE ap_mac='?' ORDER BY id DESC LIMIT 1",
		-1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	sqlite3_bind_text(stmt, 1, ap_mac, -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *s = (const char *)sqlite3_column_text(stmt, 0);
		const char *f = (const char *)sqlite3_column_text(stmt, 1);
		const char *t = (const char *)sqlite3_column_text(stmt, 2);
		const char *e = (const char *)sqlite3_column_text(stmt, 3);

		if (status_out) *status_out = 0;
		if (s && !strcmp(s, "success")) *status_out = 1;
		else if (s && !strcmp(s, "failed")) *status_out = 2;
		else if (s && !strcmp(s, "pending")) *status_out = 0;
		if (from_ver && f) strncpy(from_ver, f, from_len - 1);
		if (to_ver && t) strncpy(to_ver, t, to_len - 1);
		if (error_msg && e) strncpy(error_msg, e, err_len - 1);
		rc = 0;
	} else {
		rc = -1;
	}
	sqlite3_finalize(stmt);
	return rc;
}

/* ========================================================================
 * Audit log
 * ======================================================================== */

int sql_audit_log(const char *user, const char *action,
	const char *resource_type, const char *resource_id,
	const char *old_value, const char *new_value, const char *ip_addr)
{
	char sql_stmt[2048];
	snprintf(sql_stmt, sizeof(sql_stmt),
		"INSERT INTO audit_log (user, action, resource_type, "
		"resource_id, old_value, new_value, ip_address) "
		"VALUES ('%s', '%s', '%s', '%s', '%s', '%s', '%s')",
		user ? user : "system",
		action ? action : "",
		resource_type ? resource_type : "",
		resource_id ? resource_id : "",
		old_value ? old_value : "",
		new_value ? new_value : "",
		ip_addr ? ip_addr : "");
	return _sql_exec(sql_stmt);
}
