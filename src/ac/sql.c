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
	char sql_stmt[2048];
	char timestamp[64];
	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

	/*
	 * extra_json may contain runtime status from AP status reports:
	 *   {"online_user_num":N,"wifi_ssid":"XXXX"}
	 * Parse it and use those values instead of the stale in-memory fields.
	 */
	int db_online_users = online_users;
	char db_wifi_ssid[64] = {0};

	if (extra_json && extra_json[0] != '\0') {
		/* Parse runtime status from extra_json */
		char tmp[256];
		snprintf(tmp, sizeof(tmp), "%s", extra_json);
		char *p = tmp;
		while (*p) {
			while (*p == ' ' || *p == '\t') p++;
			if (strncmp(p, "\"online_user_num\":", 17) == 0) {
				p += 17;
				while (*p == ' ' || *p == ':' || *p == ' ') p++;
				db_online_users = atoi(p);
				while (*p && *p != ',' && *p != '}') p++;
			} else if (strncmp(p, "\"wifi_ssid\":", 12) == 0) {
				p += 12;
				while (*p == ' ' || *p == ':' || *p == '"' || *p == ' ') p++;
				char *start = p;
				while (*p && *p != '"') p++;
				int len = (int)(p - start);
				if (len > (int)sizeof(db_wifi_ssid) - 1)
					len = (int)sizeof(db_wifi_ssid) - 1;
				memcpy(db_wifi_ssid, start, len);
				db_wifi_ssid[len] = '\0';
				while (*p && *p != ',' && *p != '}') p++;
			} else {
				while (*p && *p != ',' && *p != '}') p++;
			}
			if (*p == ',') p++;
		}
	}

	/* If wifi_ssid was passed directly (not from extra_json), use it */
	if (db_wifi_ssid[0] == '\0' && wifi_ssid && wifi_ssid[0] != '\0') {
		strncpy(db_wifi_ssid, wifi_ssid, sizeof(db_wifi_ssid) - 1);
		db_wifi_ssid[sizeof(db_wifi_ssid) - 1] = '\0';
	}

	/* Use prepared statement to prevent SQL injection */
	sqlite3_stmt *stmt;
	const char *upsert_sql =
		"INSERT INTO node (mac, hostname, wan_ip, wifi_ssid, firmware, "
		"online_user_num, last_seen, time) "
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) "
		"ON CONFLICT(mac) DO UPDATE SET "
		"  hostname=COALESCE(excluded.hostname,hostname),"
		"  wan_ip=COALESCE(excluded.wan_ip,wan_ip),"
		"  wifi_ssid=COALESCE(excluded.wifi_ssid,wifi_ssid),"
		"  firmware=COALESCE(excluded.firmware,firmware),"
		"  online_user_num=?6,"
		"  last_seen=?7,"
		"  device_down=0,"
		"  time=?8";

	int rc = sqlite3_prepare_v2(sql, upsert_sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		pr_sqlerr();
		return -1;
	}

	sqlite3_bind_text(stmt, 1, mac, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, hostname ? hostname : "", -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, wan_ip ? wan_ip : "", -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 4,
		db_wifi_ssid[0] ? db_wifi_ssid : (wifi_ssid ? wifi_ssid : ""),
		-1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 5, firmware ? firmware : "", -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 6, db_online_users);
	sqlite3_bind_int64(stmt, 7, (sqlite3_int64)now);
	sqlite3_bind_text(stmt, 8, timestamp, -1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		sys_err("sql_ap_upsert failed: %s\n", sqlite3_errmsg(sql));
		return -1;
	}
	return 0;
}

int sql_ap_update_field(const char *mac, const char *field, const char *value)
{
	/*
	 * Validate field name against allowed columns to prevent injection.
	 * The field parameter comes from internal code, but defense-in-depth
	 * demands we verify it matches a real column name.
	 */
	static const char *allowed_fields[] = {
		"hostname", "wan_ip", "wan_mac", "wan_gateway",
		"wifi_iface", "wifi_ip", "wifi_mac", "wifi_ssid",
		"wifi_encryption", "wifi_key", "wifi_channel_mode",
		"wifi_channel", "wifi_signal", "firmware",
		"firmware_revision", "online_user_num", "group_id",
		"tags", "device_down", "last_seen",
		NULL
	};

	int valid = 0;
	for (int i = 0; allowed_fields[i]; i++) {
		if (strcmp(field, allowed_fields[i]) == 0) {
			valid = 1;
			break;
		}
	}
	if (!valid) {
		sys_err("sql_ap_update_field: invalid field '%s'\n", field);
		return -1;
	}

	/* Build statement with validated field name; bind value and mac */
	char sql_stmt[256];
	snprintf(sql_stmt, sizeof(sql_stmt),
		"UPDATE node SET %s=?1 WHERE mac=?2", field);

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(sql, sql_stmt, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		pr_sqlerr();
		return -1;
	}

	sqlite3_bind_text(stmt, 1, value ? value : "", -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, mac, -1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		sys_err("sql_ap_update_field failed: %s\n", sqlite3_errmsg(sql));
		return -1;
	}
	return 0;
}

int sql_ap_get_field(const char *mac, const char *field, char *out, int outlen)
{
	/* Validate field name (same whitelist as sql_ap_update_field) */
	static const char *allowed_fields[] = {
		"hostname", "wan_ip", "wan_mac", "wan_gateway",
		"wifi_iface", "wifi_ip", "wifi_mac", "wifi_ssid",
		"wifi_encryption", "wifi_key", "wifi_channel_mode",
		"wifi_channel", "wifi_signal", "firmware",
		"firmware_revision", "online_user_num", "group_id",
		"tags", "device_down", "last_seen",
		NULL
	};

	int valid = 0;
	for (int i = 0; allowed_fields[i]; i++) {
		if (strcmp(field, allowed_fields[i]) == 0) {
			valid = 1;
			break;
		}
	}
	if (!valid) {
		sys_err("sql_ap_get_field: invalid field '%s'\n", field);
		return -1;
	}

	char sql_stmt[256];
	sqlite3_stmt *stmt;
	int rc;

	snprintf(sql_stmt, sizeof(sql_stmt),
		"SELECT %s FROM node WHERE mac=?1", field);

	rc = sqlite3_prepare_v2(sql, sql_stmt, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	sqlite3_bind_text(stmt, 1, mac, -1, SQLITE_STATIC);

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
	sqlite3_stmt *stmt;
	const char *sql_stmt =
		"UPDATE node SET device_down=1, last_seen=?1 WHERE mac=?2";

	int rc = sqlite3_prepare_v2(sql, sql_stmt, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		pr_sqlerr();
		return -1;
	}

	sqlite3_bind_int64(stmt, 1, (sqlite3_int64)time(NULL));
	sqlite3_bind_text(stmt, 2, mac, -1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		sys_err("sql_ap_set_offline failed: %s\n", sqlite3_errmsg(sql));
		return -1;
	}
	return 0;
}

/* ========================================================================
 * AP Group operations
 * ======================================================================== */

int sql_group_create(const char *name, const char *description)
{
	sqlite3_stmt *stmt;
	const char *sql_stmt =
		"INSERT INTO ap_group (name, description) VALUES (?1, ?2)";

	int rc = sqlite3_prepare_v2(sql, sql_stmt, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		pr_sqlerr();
		return -1;
	}

	sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, description ? description : "", -1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		sys_err("sql_group_create failed: %s\n", sqlite3_errmsg(sql));
		return -1;
	}
	return 0;
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
	sqlite3_stmt *stmt;
	const char *sql_stmt = "UPDATE node SET group_id=?1 WHERE mac=?2";

	int rc = sqlite3_prepare_v2(sql, sql_stmt, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		pr_sqlerr();
		return -1;
	}

	sqlite3_bind_int(stmt, 1, group_id);
	sqlite3_bind_text(stmt, 2, mac, -1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		sys_err("sql_group_add_ap failed: %s\n", sqlite3_errmsg(sql));
		return -1;
	}
	return 0;
}

int sql_group_remove_ap(const char *mac, int group_id)
{
	(void)group_id;
	sqlite3_stmt *stmt;
	const char *sql_stmt = "UPDATE node SET group_id=0 WHERE mac=?1";

	int rc = sqlite3_prepare_v2(sql, sql_stmt, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		pr_sqlerr();
		return -1;
	}

	sqlite3_bind_text(stmt, 1, mac, -1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		sys_err("sql_group_remove_ap failed: %s\n", sqlite3_errmsg(sql));
		return -1;
	}
	return 0;
}

/* ========================================================================
 * Alarm operations
 * ======================================================================== */

int sql_alarm_insert(int level, const char *ap_mac, const char *message,
	const char *raw_data)
{
	sqlite3_stmt *stmt;
	const char *sql_stmt =
		"INSERT INTO alarm_event (ap_mac, level, message, raw_data) "
		"VALUES (?1, ?2, ?3, ?4)";

	int rc = sqlite3_prepare_v2(sql, sql_stmt, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		pr_sqlerr();
		return -1;
	}

	sqlite3_bind_text(stmt, 1, ap_mac ? ap_mac : "unknown", -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 2, level);
	sqlite3_bind_text(stmt, 3, message ? message : "", -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 4, raw_data ? raw_data : "", -1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		sys_err("sql_alarm_insert failed: %s\n", sqlite3_errmsg(sql));
		return -1;
	}
	return 0;
}

int sql_alarm_ack(int alarm_id, const char *acked_by)
{
	sqlite3_stmt *stmt;
	char timestamp[64];
	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

	const char *sql_stmt =
		"UPDATE alarm_event SET acknowledged=1,"
		"acknowledged_by=?1, acknowledged_at=?2 WHERE id=?3";

	int rc = sqlite3_prepare_v2(sql, sql_stmt, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		pr_sqlerr();
		return -1;
	}

	sqlite3_bind_text(stmt, 1, acked_by ? acked_by : "system", -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, timestamp, -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 3, alarm_id);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		sys_err("sql_alarm_ack failed: %s\n", sqlite3_errmsg(sql));
		return -1;
	}
	return 0;
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
	sqlite3_stmt *stmt;
	const char *sql_stmt =
		"INSERT INTO firmware (version, filename, file_size, sha256) "
		"VALUES (?1, ?2, ?3, ?4)";

	int rc = sqlite3_prepare_v2(sql, sql_stmt, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		pr_sqlerr();
		return -1;
	}

	sqlite3_bind_text(stmt, 1, version, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, filename ? filename : "", -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 3, (int)file_size);
	sqlite3_bind_text(stmt, 4, sha256 ? sha256 : "", -1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		sys_err("sql_firmware_insert failed: %s\n", sqlite3_errmsg(sql));
		return -1;
	}
	return 0;
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
	sqlite3_stmt *stmt;
	const char *sql_stmt =
		"INSERT INTO upgrade_log (ap_mac, from_version, to_version, status) "
		"VALUES (?1, ?2, ?3, 'pending')";

	int rc = sqlite3_prepare_v2(sql, sql_stmt, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		pr_sqlerr();
		return -1;
	}

	sqlite3_bind_text(stmt, 1, ap_mac, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, from_ver ? from_ver : "", -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, to_ver ? to_ver : "", -1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		sys_err("sql_upgrade_start failed: %s\n", sqlite3_errmsg(sql));
		return -1;
	}
	return 0;
}

int sql_upgrade_finish(const char *ap_mac, const char *status, const char *error_msg)
{
	sqlite3_stmt *stmt;
	char timestamp[64];
	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

	const char *sql_stmt =
		"UPDATE upgrade_log SET status=?1,finished_at=?2,"
		"error_message=?3 WHERE ap_mac=?4 AND status='pending'";

	int rc = sqlite3_prepare_v2(sql, sql_stmt, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		pr_sqlerr();
		return -1;
	}

	sqlite3_bind_text(stmt, 1, status ? status : "unknown", -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, timestamp, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, error_msg ? error_msg : "", -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 4, ap_mac, -1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		sys_err("sql_upgrade_finish failed: %s\n", sqlite3_errmsg(sql));
		return -1;
	}
	return 0;
}

int sql_upgrade_progress(const char *ap_mac, int *status_out,
	char *from_ver, int from_len, char *to_ver, int to_len,
	char *error_msg, int err_len)
{
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(sql,
		"SELECT status,from_version,to_version,error_message "
		"FROM upgrade_log WHERE ap_mac=?1 ORDER BY id DESC LIMIT 1",
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
	sqlite3_stmt *stmt;
	const char *sql_stmt =
		"INSERT INTO audit_log (user, action, resource_type, "
		"resource_id, old_value, new_value, ip_address) "
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)";

	int rc = sqlite3_prepare_v2(sql, sql_stmt, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		pr_sqlerr();
		return -1;
	}

	sqlite3_bind_text(stmt, 1, user ? user : "system", -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, action ? action : "", -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, resource_type ? resource_type : "", -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 4, resource_id ? resource_id : "", -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 5, old_value ? old_value : "", -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 6, new_value ? new_value : "", -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 7, ip_addr ? ip_addr : "", -1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		sys_err("sql_audit_log failed: %s\n", sqlite3_errmsg(sql));
		return -1;
	}
	return 0;
}
