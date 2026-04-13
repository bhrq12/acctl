/*
 * ============================================================================
 *
 *       Filename:  db.h
 *
 *    Description:  JSON file-based database for AC Controller
 *                  Replaces SQLite with zero external dependencies
 *
 *        Version:  2.1
 *        Created:  2026-04-13
 *       Compiler:  gcc
 *
 * ============================================================================
 */
#ifndef __DB_H__
#define __DB_H__

#include <json-c/json.h>

#define DB_NULL     "(null)"
#define DBNAME      "/etc/acctl/ac.json"
#define DB_BACKUP   "/etc/acctl/ac.json.bak"
#define RESOURCE    "resource"

#define GETRES      "resource"  /* JSON key for resource data */

#define pr_dberr()  \
    sys_err("DB Error: %s\n", db_last_error())

#define COLMAX      (128)

struct col_name_t {
    char name[128];
};

struct tbl_dsc_t {
    unsigned int col_num;
    struct col_name_t *head;
};

struct tbl_col_t {
    struct tbl_dsc_t res;
};

/* Database handle - wraps JSON root object */
typedef struct {
    json_object *root;
    char *last_error;
    int modified;
} db_t;

extern db_t *db;
extern struct tbl_col_t tables;

/* Core database operations */
int db_init(db_t **dbp);
void db_close(db_t *dbp);
const char *db_last_error(void);
int db_save(db_t *dbp);

/* Resource operations */
int db_query_res(db_t *dbp, char *buffer, int len);

/* AP operations */
int db_ap_upsert(const char *mac, const char *hostname,
    const char *wan_ip, const char *wifi_ssid,
    const char *firmware, int online_users, const char *extra_json);
int db_ap_update_field(const char *mac, const char *field, const char *value);
int db_ap_get_field(const char *mac, const char *field, char *out, int outlen);
int db_ap_set_offline(const char *mac);

/* AP Group operations */
int db_group_create(const char *name, const char *description);
int db_group_delete(int group_id);
int db_group_list(char *json_buf, int buflen);
int db_group_add_ap(const char *mac, int group_id);
int db_group_remove_ap(const char *mac, int group_id);

/* Alarm operations */
int db_alarm_insert(int level, const char *ap_mac, const char *message,
    const char *raw_data);
int db_alarm_ack(int alarm_id, const char *acked_by);
int db_alarm_list(char *json_buf, int buflen, int limit);
int db_alarm_count_by_level(void);

/* Firmware operations */
int db_firmware_insert(const char *version, const char *filename,
    uint32_t file_size, const char *sha256);
int db_firmware_list(char *json_buf, int buflen);
int db_firmware_getlatest(char *version_out, int version_len);

/* Upgrade log */
int db_upgrade_start(const char *ap_mac, const char *from_ver, const char *to_ver);
int db_upgrade_finish(const char *ap_mac, const char *status, const char *error_msg);
int db_upgrade_progress(const char *ap_mac, int *status_out,
    char *from_ver, int from_len, char *to_ver, int to_len,
    char *error_msg, int err_len);

/* Audit log */
int db_audit_log(const char *user, const char *action,
    const char *resource_type, const char *resource_id,
    const char *old_value, const char *new_value, const char *ip_addr);

/* Table column info (for compatibility) */
void db_tbl_col(db_t *dbp);

#endif /* __DB_H__ */
