/*
 * ============================================================================
 *
 *       Filename:  db.h
 *
 *    Description:  JSON file-based database for AC Controller.
 *                  Replaces SQLite with zero external dependencies.
 *                  Data stored in /etc/acctl/ac.json
 *
 *        Version:  2.0
 *        Created:  2026-04-13
 *       Revision:  full implementation replacing sql.c
 *       Compiler:  gcc
 *
 * ============================================================================
 */
#ifndef __DB_H__
#define __DB_H__

#include <stdint.h>
#include <json-c/json.h>
#include "log.h"

#define DB_NULL       "(null)"
#define DBNAME        "/etc/acctl/ac.json"
#define DB_BACKUP     "/etc/acctl/ac.json.bak"
#define COLMAX        (128)

#define GETRES        "resource"  /* JSON key for resource data */

#define pr_dberr()    \
    sys_err("DB Error: %s\n", db_last_error())

/* Column name cache — mirrors old tbl_col_t for compatibility */
struct col_name_t {
    char name[COLMAX];
};

struct tbl_dsc_t {
    unsigned int col_num;
    struct col_name_t *head;
};

struct tbl_col_t {
    struct tbl_dsc_t res;
};

/* Database handle */
typedef struct {
    json_object *root;
    char last_error[256];
    int modified;
} db_t;

extern db_t  *db;
extern struct tbl_col_t tables;

/* json_attrs for resource.c — defined in resource.c */

/* ========================================================================
 * Core database operations
 * ======================================================================== */

int   db_init(db_t **dbp);
void  db_close(db_t *dbp);
int   db_save(db_t *dbp);
void  db_tbl_col(db_t *dbp);
const char *db_last_error(void);

/* ========================================================================
 * Resource — IP pool configuration
 * ======================================================================== */

int db_query_res(db_t *dbp, char *buffer, int len);

/* ========================================================================
 * AP operations — node/device management
 * ======================================================================== */

/* Upsert AP (insert or update on conflict by mac) */
int db_ap_upsert(const char *mac, const char *hostname,
    const char *wan_ip, const char *wifi_ssid,
    const char *firmware, int online_users, const char *extra_json);

/* Update a single field by name (field whitelist enforced) */
int db_ap_update_field(const char *mac, const char *field, const char *value);

/* Read a single field value */
int db_ap_get_field(const char *mac, const char *field, char *out, int outlen);

/* Mark AP as offline */
int db_ap_set_offline(const char *mac);

/* ========================================================================
 * AP Group operations
 * ======================================================================== */

int db_group_create(const char *name, const char *description);
int db_group_delete(int group_id);
int db_group_list(char *json_buf, int buflen);
int db_group_add_ap(const char *mac, int group_id);
int db_group_remove_ap(const char *mac, int group_id);

/* ========================================================================
 * Alarm / event operations
 * ======================================================================== */

int db_alarm_insert(int level, const char *ap_mac,
    const char *message, const char *raw_data);
int db_alarm_ack(int alarm_id, const char *acked_by);
int db_alarm_list(char *json_buf, int buflen, int limit);
int db_alarm_count_by_level(void);

/* ========================================================================
 * Firmware repository operations
 * ======================================================================== */

int db_firmware_insert(const char *version, const char *filename,
    uint32_t file_size, const char *sha256);
int db_firmware_list(char *json_buf, int buflen);
int db_firmware_getlatest(char *version_out, int version_len);

/* ========================================================================
 * Upgrade tracking
 * ======================================================================== */

int db_upgrade_start(const char *ap_mac, const char *from_ver,
    const char *to_ver);
int db_upgrade_finish(const char *ap_mac, const char *status,
    const char *error_msg);
int db_upgrade_progress(const char *ap_mac, int *status_out,
    char *from_ver, int from_len, char *to_ver, int to_len,
    char *error_msg, int err_len);

/* ========================================================================
 * Audit log
 * ======================================================================== */

int db_audit_log(const char *user, const char *action,
    const char *resource_type, const char *resource_id,
    const char *old_value, const char *new_value,
    const char *ip_addr);

#endif /* __DB_H__ */
