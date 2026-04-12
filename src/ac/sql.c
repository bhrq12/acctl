/*
 * ============================================================================
 *
 *       Filename:  sql.c
 *
 *    Description:  SQLite implementation for AC controller
 *
 *        Version:  1.0
 *        Created:  2014年11月12日 15时49分05秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  jianxi sun (jianxi), ycsunjane@gmail.com
 *   Organization:  
 *
 * ============================================================================
 */
#include <stdio.h>
#include <stdint.h>
#include <sqlite3.h>
#include "sql.h"
#include "mjson.h"
#include "log.h"
#include <string.h>

sqlite3 *sql = NULL;
struct tbl_col_t tables;

int sql_query_res(SQL *sql, char *buffer, int len)
{
	sqlite3_stmt *stmt;
	int rc;
	const char *tail;
	
	rc = sqlite3_prepare_v2(sql, GETRES, -1, &stmt, &tail);
	if(rc != SQLITE_OK) {
		pr_sqlerr();
		return -1;
	}

	rc = sqlite3_step(stmt);
	if(rc != SQLITE_ROW) {
		sys_warn("Have no resource in database\n");
		sqlite3_finalize(stmt);
		return -1;
	}

	int i;
	int col_count = sqlite3_column_count(stmt);
	struct col_name_t *col = tables.res.head;
	
	JSON_ENCODE_START(buffer, len);
	
	for(i = 0; i < col_count && i < tables.res.col_num && len > 2; i++) {
		const char *value = (const char *)sqlite3_column_text(stmt, i);
		if(value == NULL) {
			value = SQL_NULL;
		}
		JSON_ENCODE(buffer, len, col[i].name, value);
	}
	
	JSON_ENCODE_END(buffer, len);
	
	sqlite3_finalize(stmt);
	return 0;
}

void sql_close(SQL *sql)
{
	if(sql) {
		sqlite3_close(sql);
		sql = NULL;
	}
}

void _sql_tbl_col(SQL *sql, char *table, struct tbl_dsc_t *dsc)
{
	char sql_cmd[256];
	sqlite3_stmt *stmt;
	int rc;
	const char *tail;
	
	snprintf(sql_cmd, sizeof(sql_cmd), "SELECT * FROM %s LIMIT 1", table);
	rc = sqlite3_prepare_v2(sql, sql_cmd, -1, &stmt, &tail);
	if(rc != SQLITE_OK) {
		pr_sqlerr();
		exit(-1);
	}

	dsc->col_num = sqlite3_column_count(stmt);
	dsc->head = malloc(sizeof(struct col_name_t) * dsc->col_num);
	if(!dsc->head) {
		sys_err("Malloc failed: %s(%d)\n", 
			strerror(errno), errno);
		exit(-1);
	}

	int i;
	for(i = 0; i < dsc->col_num; i++) {
		const char *col_name = sqlite3_column_name(stmt, i);
		if(col_name) {
			strncpy(dsc->head[i].name, col_name, COLMAX - 1);
			dsc->head[i].name[COLMAX - 1] = '\0';
		}
	}

	sqlite3_finalize(stmt);
}

void sql_tbl_col(SQL *sql)
{
	_sql_tbl_col(sql, RESOURCE, &tables.res);
}

int sql_init(SQL *db)
{
	int rc;
	char *err_msg = NULL;
	
	printf("SQLite version:%s\n", sqlite3_libversion());
	
	rc = sqlite3_open(DBNAME, &sql);
	if(rc != SQLITE_OK) {
		sys_err("Can't open database: %s\n", sqlite3_errmsg(sql));
		exit(-1);
	}

	const char *create_resource = 
		"CREATE TABLE IF NOT EXISTS resource ("
		"  ip_start TEXT,"
		"  ip_end TEXT,"
		"  ip_mask TEXT"
		");";
	
	rc = sqlite3_exec(sql, create_resource, NULL, NULL, &err_msg);
	if(rc != SQLITE_OK) {
		sys_err("SQL error: %s\n", err_msg);
		sqlite3_free(err_msg);
		exit(-1);
	}

	const char *create_node = 
		"CREATE TABLE IF NOT EXISTS node ("
		"  hostname TEXT,"
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
		"  wan_mac TEXT UNIQUE,"
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
		"  online_user_num INTEGER DEFAULT 0"
		");";
	
	rc = sqlite3_exec(sql, create_node, NULL, NULL, &err_msg);
	if(rc != SQLITE_OK) {
		sys_err("SQL error: %s\n", err_msg);
		sqlite3_free(err_msg);
		exit(-1);
	}

	const char *create_node_default = 
		"CREATE TABLE IF NOT EXISTS node_default ("
		"  profile TEXT PRIMARY KEY,"
		"  device_name TEXT,"
		"  wifi_ssid TEXT NOT NULL,"
		"  wifi_encryption TEXT NOT NULL,"
		"  wifi_key TEXT NOT NULL,"
		"  wifi_channel_mode TEXT NOT NULL,"
		"  wifi_channel TEXT,"
		"  wifi_signal TEXT"
		");";
	
	rc = sqlite3_exec(sql, create_node_default, NULL, NULL, &err_msg);
	if(rc != SQLITE_OK) {
		sys_err("SQL error: %s\n", err_msg);
		sqlite3_free(err_msg);
		exit(-1);
	}

	const char *create_node_setting = 
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
		");";
	
	rc = sqlite3_exec(sql, create_node_setting, NULL, NULL, &err_msg);
	if(rc != SQLITE_OK) {
		sys_err("SQL error: %s\n", err_msg);
		sqlite3_free(err_msg);
		exit(-1);
	}

	sql_tbl_col(sql);
	return 0;
}
