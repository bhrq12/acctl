/*
 * =====================================================================================
 *
 *       Filename:  sql.h
 *
 *    Description:  SQLite database interface declarations
 *
 *        Version:  2.0
 *        Created:  2026-04-12
 *       Revision:  extended for AP groups, alarms, firmware, audit
 *       Compiler:  gcc
 *
 *         Author:  jianxi sun (jianxi), ycsunjane@gmail.com
 *   Organization:  OpenWrt AC Controller Project
 *
 * =====================================================================================
 */
#ifndef __SQL_H__
#define __SQL_H__

#include <sqlite3.h>
#include <stdint.h>

#define DBNAME         "/etc/acctl/ac.db"
#define RESOURCE       "resource"
#define COLMAX         (64)
#define SQL_NULL       "NULL"

typedef sqlite3* SQL;

struct col_name_t {
	char name[COLMAX];
};

struct tbl_col_t {
	struct {
		struct col_name_t *head;
		int col_num;
	} res;
	struct {
		struct col_name_t *head;
		int col_num;
	} node;
	struct {
		struct col_name_t *head;
		int col_num;
	} node_default;
	struct {
		struct col_name_t *head;
		int col_num;
	} node_setting;
	struct {
		struct col_name_t *head;
		int col_num;
	} ap_group;
	struct {
		struct col_name_t *head;
		int col_num;
	} ap_group_map;
	struct {
		struct col_name_t *head;
		int col_num;
	} alarm_event;
	struct {
		struct col_name_t *head;
		int col_num;
	} firmware;
	struct {
		struct col_name_t *head;
		int col_num;
	} upgrade_log;
	struct {
		struct col_name_t *head;
		int col_num;
	} audit_log;
};

extern struct tbl_col_t tables;
extern SQL sql;

int  sql_init(SQL db);
void sql_close(SQL db);
void sql_tbl_col(SQL db);
int  sql_query_res(SQL db, char *buffer, int len);

/* AP operations */
int  sql_ap_upsert(const char *mac, const char *hostname, const char *wan_ip,
                    const char *wifi_ssid, const char *firmware,
                    int online_users, const char *extra_json);
int  sql_ap_update_field(const char *mac, const char *field, const char *value);
int  sql_ap_get_field(const char *mac, const char *field, char *out, int outlen);
int  sql_ap_set_offline(const char *mac);

/* AP group operations */
int  sql_group_create(const char *name, const char *description);
int  sql_group_delete(int group_id);
int  sql_group_list(char *json_buf, int buflen);
int  sql_group_add_ap(const char *mac, int group_id);
int  sql_group_remove_ap(const char *mac, int group_id);

/* Alarm operations */
int  sql_alarm_insert(int level, const char *ap_mac, const char *message,
                      const char *raw_data);
int  sql_alarm_ack(int alarm_id, const char *acked_by);
int  sql_alarm_list(char *json_buf, int buflen, int limit);
int  sql_alarm_count_by_level(void);

/* Firmware operations */
int  sql_firmware_insert(const char *version, const char *filename,
                         uint32_t file_size, const char *sha256);
int  sql_firmware_list(char *json_buf, int buflen);
int  sql_firmware_getlatest(char *version_out, int version_len);

/* Upgrade log operations */
int  sql_upgrade_start(const char *ap_mac, const char *from_ver,
                       const char *to_ver);
int  sql_upgrade_finish(const char *ap_mac, const char *status,
                        const char *error_msg);
int  sql_upgrade_progress(const char *ap_mac, int *status_out,
                          char *from_ver, int from_len, char *to_ver,
                          int to_len, char *error_msg, int err_len);

/* Audit log */
int  sql_audit_log(const char *user, const char *action,
                   const char *resource_type, const char *resource_id,
                   const char *old_value, const char *new_value,
                   const char *ip_addr);

/* JSON helpers */
#define GETRES  "SELECT ip_start,ip_end,ip_mask FROM resource LIMIT 1"

#define JSON_ENCODE_START(buf, len) \
	do { ((buf)[0] = '{'); (buf)[1] = '\0'; (len)--; } while(0)

#define JSON_ENCODE(buf, len, key, val) \
	do { \
		int _n = snprintf((buf), (len), "%s\"%s\":\"%s\"", \
			(len) > 1 && (buf)[0] == '{' ? "" : ",", (key), (val)); \
		if(_n > 0) { (buf) += _n; (len) -= _n; } \
	} while(0)

#define JSON_ENCODE_INT(buf, len, key, val) \
	do { \
		int _n = snprintf((buf), (len), "%s\"%s\":%d", \
			(len) > 1 && (buf)[0] == '{' ? "" : ",", (key), (val)); \
		if(_n > 0) { (buf) += _n; (len) -= _n; } \
	} while(0)

#define JSON_ENCODE_END(buf, len) \
	do { \
		if((len) > 1) { \
			(buf)[0] = '}'; (buf)[1] = '\0'; \
		} \
	} while(0)

#define pr_sqlerr() \
	sys_err("SQLite error: %s\n", sqlite3_errmsg(sql))

/* json_attrs for mjson parser */
extern const char *json_attrs[];

#endif /* __SQL_H__ */
