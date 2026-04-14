/*
 * ============================================================================
 *
 *       Filename:  db.c
 *
 *    Description:  JSON file-based database for AC Controller.
 *                  Replaces SQLite with zero external dependencies.
 *                  All data stored in /etc/acctl/ac.json
 *
 *        Version:  2.0
 *        Created:  2026-04-13
 *       Revision:  full implementation — replaces sql.c
 *       Compiler:  gcc
 *
 * ============================================================================
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>

#include "db.h"
#include "resource.h"
#include "log.h"

/* Global database handle */
db_t *db = NULL;
struct tbl_col_t tables;

/* json_attrs moved to resource.c with correct type */

/* Static error buffer */
static char error_buf[256];

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

const char *db_last_error(void)
{
    return error_buf[0] ? error_buf : "Unknown error";
}

static void set_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(error_buf, sizeof(error_buf), fmt, ap);
    va_end(ap);
}

static int file_lock(int fd, int lock_type)
{
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = lock_type;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;
    return fcntl(fd, F_SETLKW, &fl);
}

/* Ensure /etc/acctl directory exists */
static int ensure_db_dir(void)
{
    if (access("/etc/acctl", F_OK) != 0) {
        if (mkdir("/etc/acctl", 0755) != 0 && errno != EEXIST) {
            set_error("Cannot create /etc/acctl: %s", strerror(errno));
            return -1;
        }
    }
    return 0;
}

/* Load JSON file with locking */
static json_object *json_load_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;

    file_lock(fd, F_RDLCK);

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size == 0) {
        file_lock(fd, F_UNLCK);
        close(fd);
        return NULL;
    }

    char *buf = malloc(st.st_size + 1);
    if (!buf) {
        file_lock(fd, F_UNLCK);
        close(fd);
        return NULL;
    }

    ssize_t n = read(fd, buf, st.st_size);
    file_lock(fd, F_UNLCK);
    close(fd);

    if (n != st.st_size) {
        free(buf);
        return NULL;
    }
    buf[st.st_size] = '\0';

    json_object *obj = json_tokener_parse(buf);
    free(buf);
    return obj;
}

/* Save JSON file with locking and backup */
static int json_save_file(const char *path, json_object *obj)
{
    /* Create backup first */
    rename(path, DB_BACKUP);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        rename(DB_BACKUP, path);
        set_error("Cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    file_lock(fd, F_WRLCK);

    const char *str = json_object_to_json_string_ext(obj,
        JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_SPACED);

    ssize_t written = write(fd, str, strlen(str));
    write(fd, "\n", 1);

    file_lock(fd, F_UNLCK);
    close(fd);

    if (written < 0) {
        rename(DB_BACKUP, path);
        set_error("Write error: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/* Get top-level object by key, returns borrowed reference */
static json_object *get_root_obj(const char *key)
{
    if (!db || !db->root) return NULL;
    json_object *obj;
    if (!json_object_object_get_ex(db->root, key, &obj))
        return NULL;
    return obj;
}

/* Get or create top-level array */
static json_object *get_or_create_array(const char *key)
{
    json_object *obj = get_root_obj(key);
    if (obj) return obj;

    /* Create it */
    json_object *arr = json_object_new_array();
    json_object_object_add(db->root, key, arr);
    db->modified = 1;
    return arr;
}

/* Find object in array by key=value */
static json_object *find_in_array(json_object *arr, const char *key, const char *value)
{
    if (!arr || !json_object_is_type(arr, json_type_array))
        return NULL;

    int len = json_object_array_length(arr);
    for (int i = 0; i < len; i++) {
        json_object *item = json_object_array_get_idx(arr, i);
        json_object *jv;
        if (json_object_object_get_ex(item, key, &jv)) {
            const char *sv = json_object_get_string(jv);
            if (sv && strcmp(sv, value) == 0)
                return item;
        }
    }
    return NULL;
}

/* Find object in array by int key=value */
static json_object *find_in_array_int(json_object *arr, const char *key, int ival)
{
    if (!arr || !json_object_is_type(arr, json_type_array))
        return NULL;

    int len = json_object_array_length(arr);
    for (int i = 0; i < len; i++) {
        json_object *item = json_object_array_get_idx(arr, i);
        json_object *jv;
        if (json_object_object_get_ex(item, key, &jv)) {
            if (json_object_get_int(jv) == ival)
                return item;
        }
    }
    return NULL;
}

/* Find object in array by key=int_value */
static int find_index_in_array_int(json_object *arr, const char *key, int ival)
{
    if (!arr || !json_object_is_type(arr, json_type_array))
        return -1;

    int len = json_object_array_length(arr);
    for (int i = 0; i < len; i++) {
        json_object *item = json_object_array_get_idx(arr, i);
        json_object *jv;
        if (json_object_object_get_ex(item, key, &jv)) {
            if (json_object_get_int(jv) == ival)
                return i;
        }
    }
    return -1;
}

/* Helper: get string from object, returns "" if null/missing */
static const char *safe_str(json_object *obj, const char *key)
{
    json_object *jv;
    if (!json_object_object_get_ex(obj, key, &jv))
        return "";
    if (!jv || json_object_get_type(jv) == json_type_null)
        return "";
    return json_object_get_string(jv);
}

/* Helper: set string field, replacing if exists */
static void set_str(json_object *obj, const char *key, const char *val)
{
    json_object_object_add(obj, key,
        json_object_new_string(val ? val : ""));
}

/* Helper: set int field */
static void set_int(json_object *obj, const char *key, int val)
{
    json_object_object_add(obj, key, json_object_new_int(val));
}

/* Helper: get int field */
static int get_int(json_object *obj, const char *key)
{
    json_object *jv;
    if (!json_object_object_get_ex(obj, key, &jv))
        return 0;
    return json_object_get_int(jv);
}

/* Helper: get int64 field */
static int64_t get_int64(json_object *obj, const char *key)
{
    json_object *jv;
    if (!json_object_object_get_ex(obj, key, &jv))
        return 0;
    return json_object_get_int64(jv);
}

/* Helper: set int64 field */
static void set_int64(json_object *obj, const char *key, int64_t val)
{
    json_object_object_add(obj, key,
        json_object_new_int64(val));
}

/* Helper: delete object from array by key=value */
static int del_from_array(json_object *arr, const char *key, const char *value)
{
    if (!arr || !json_object_is_type(arr, json_type_array))
        return -1;

    int len = json_object_array_length(arr);
    for (int i = 0; i < len; i++) {
        json_object *item = json_object_array_get_idx(arr, i);
        json_object *jv;
        if (json_object_object_get_ex(item, key, &jv)) {
            const char *sv = json_object_get_string(jv);
            if (sv && strcmp(sv, value) == 0) {
                json_object_array_put_idx(arr, i, NULL);
                json_object_array_del_idx(arr, i, 1);
                db->modified = 1;
                return 0;
            }
        }
    }
    return -1;
}

/* Helper: escape JSON string value */
static void json_escape_append(char *dest, int *dest_len, int dest_cap,
    const char *key, const char *val)
{
    if (*dest_len >= dest_cap - 1) return;
    char *dp = dest + *dest_len;
    int space = dest_cap - *dest_len;

    int pos = 0;
    if (*dest_len == 0 || dest[0] != '{') {
        dp[pos++] = '{';
    } else {
        dp[pos++] = ',';
    }

    /* "key":" */
    int klen = strlen(key);
    int vlen = strlen(val ? val : "");
    if (pos + klen + vlen + 6 >= space) {
        *dest_len = dest_cap;
        return;
    }

    dp[pos++] = '"';
    memcpy(dp + pos, key, klen);
    pos += klen;
    dp[pos++] = '"';
    dp[pos++] = ':';
    dp[pos++] = '"';

    /* Value with basic escape */
    const char *p = val ? val : "";
    while (*p && pos < space - 3) {
        if (*p == '"' || *p == '\\') {
            dp[pos++] = '\\';
            if (pos < space - 1) dp[pos++] = *p;
        } else {
            dp[pos++] = *p;
        }
        p++;
    }
    dp[pos++] = '"';
    dp[pos] = '\0';
    *dest_len += pos;
}

/* ========================================================================
 * Default schema creation
 * ======================================================================== */

static json_object *create_default_schema(void)
{
    json_object *root = json_object_new_object();

    /* Resource — IP pool configuration */
    json_object *resource = json_object_new_object();
    json_object_object_add(resource, "ip_start", json_object_new_string(""));
    json_object_object_add(resource, "ip_end",   json_object_new_string(""));
    json_object_object_add(resource, "ip_mask",  json_object_new_string(""));
    json_object_object_add(root, "resource", resource);

    /* Nodes — AP devices */
    json_object_object_add(root, "nodes", json_object_new_array());

    /* Node defaults — AP config templates */
    json_object_object_add(root, "node_defaults", json_object_new_array());

    /* Node settings — AP pre-configuration */
    json_object_object_add(root, "node_settings", json_object_new_array());

    /* AP groups */
    json_object_object_add(root, "ap_groups", json_object_new_array());

    /* Alarm events */
    json_object_object_add(root, "alarm_events", json_object_new_array());

    /* Firmwares */
    json_object_object_add(root, "firmwares", json_object_new_array());

    /* Upgrade logs */
    json_object_object_add(root, "upgrade_logs", json_object_new_array());

    /* Audit logs */
    json_object_object_add(root, "audit_logs", json_object_new_array());

    /* Metadata */
    json_object *meta = json_object_new_object();
    json_object_object_add(meta, "version",    json_object_new_string("2.0"));
    json_object_object_add(meta, "created_at", json_object_new_string(""));
    json_object_object_add(root, "_meta", meta);

    return root;
}

/* ========================================================================
 * Database initialization and persistence
 * ======================================================================== */

int db_save(db_t *dbp)
{
    if (!dbp || !dbp->root)
        return -1;

    if (json_save_file(DBNAME, dbp->root) != 0)
        return -1;

    dbp->modified = 0;
    sys_info("Database saved to %s\n", DBNAME);
    return 0;
}

int db_init(db_t **dbp)
{
    db_t *newdb = calloc(1, sizeof(db_t));
    if (!newdb) {
        set_error("Out of memory");
        return -1;
    }

    if (ensure_db_dir() != 0) {
        free(newdb);
        return -1;
    }

    /* Try to load existing database */
    newdb->root = json_load_file(DBNAME);

    /* Create default schema if load failed or file is empty */
    if (!newdb->root) {
        newdb->root = create_default_schema();
        sys_info("Created new JSON database at %s\n", DBNAME);
        db_save(newdb);
    }

    /* Initialize column cache */
    tables.res.head = malloc(sizeof(struct col_name_t) * COLMAX);
    tables.res.col_num = 3;
    strncpy(tables.res.head[0].name, "ip_start", COLMAX - 1);
    strncpy(tables.res.head[1].name, "ip_end",   COLMAX - 1);
    strncpy(tables.res.head[2].name, "ip_mask",  COLMAX - 1);
    tables.res.head[0].name[COLMAX - 1] = '\0';
    tables.res.head[1].name[COLMAX - 1] = '\0';
    tables.res.head[2].name[COLMAX - 1] = '\0';

    if (dbp)
        *dbp = newdb;
    db = newdb;

    sys_info("JSON database initialized: %s\n", DBNAME);
    return 0;
}

void db_close(db_t *dbp)
{
    if (!dbp) dbp = db;
    if (!dbp) return;

    if (dbp->modified)
        db_save(dbp);

    if (dbp->root)
        json_object_put(dbp->root);

    free(dbp);
    if (dbp == db) db = NULL;
}

void db_tbl_col(db_t *dbp)
{
    (void)dbp;
    /* Column info already cached in tables global */
}

/* ========================================================================
 * Resource query — IP pool config
 * ======================================================================== */

int db_query_res(db_t *dbp, char *buffer, int len)
{
    (void)dbp;
    if (!db || !db->root)
        return -1;

    json_object *res = get_root_obj("resource");
    if (!res)
        return -1;

    JSON_ENCODE_START(buffer, len);

    for (int i = 0; i < tables.res.col_num && len > 2; i++) {
        const char *key = tables.res.head[i].name;
        json_object *jv;
        if (json_object_object_get_ex(res, key, &jv)) {
            const char *val = json_object_get_string(jv);
            JSON_ENCODE(buffer, len, key, val ? val : SQL_NULL);
        }
    }

    JSON_ENCODE_END(buffer, len);
    return 0;
}

/* ========================================================================
 * AP operations
 * ======================================================================== */

int db_ap_upsert(const char *mac, const char *hostname,
    const char *wan_ip, const char *wifi_ssid,
    const char *firmware, int online_users, const char *extra_json)
{
    if (!db || !db->root || !mac) return -1;

    json_object *nodes = get_or_create_array("nodes");
    json_object *node  = find_in_array(nodes, "mac", mac);

    time_t now = time(NULL);

    /* Parse extra_json for runtime fields */
    int db_online_users = online_users;
    char db_wifi_ssid[64] = {0};

    if (extra_json && extra_json[0] != '\0') {
        json_object *ej = json_tokener_parse(extra_json);
        if (ej && json_object_is_type(ej, json_type_object)) {
            json_object *oun;
            if (json_object_object_get_ex(ej, "online_user_num", &oun)) {
                db_online_users = json_object_get_int(oun);
            }
            json_object *wss;
            if (json_object_object_get_ex(ej, "wifi_ssid", &wss)) {
                const char *s = json_object_get_string(wss);
                if (s) {
                    strncpy(db_wifi_ssid, s, sizeof(db_wifi_ssid) - 1);
                    db_wifi_ssid[sizeof(db_wifi_ssid) - 1] = '\0';
                }
            }
        }
        if (ej) json_object_put(ej);
    }

    if (!node) {
        /* Insert new */
        node = json_object_new_object();
        json_object_object_add(node, "mac",       json_object_new_string(mac));
        json_object_object_add(node, "time_first",json_object_new_int64(now));
        json_object_array_add(nodes, node);
    }

    /* Update fields */
    if (hostname)    set_str(node, "hostname",      hostname);
    if (wan_ip)     set_str(node, "wan_ip",         wan_ip);

    if (db_wifi_ssid[0])
        set_str(node, "wifi_ssid", db_wifi_ssid);
    else if (wifi_ssid && wifi_ssid[0])
        set_str(node, "wifi_ssid", wifi_ssid);

    if (firmware)   set_str(node, "firmware",       firmware);

    set_int(node,   "online_user_num", db_online_users);
    set_int(node,   "device_down",    0);
    set_int64(node, "last_seen",      (int64_t)now);

    /* time field: format as ISO timestamp for compatibility */
    char ts[64];
    struct tm *tm_info = localtime(&now);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);
    set_str(node, "time", ts);

    db->modified = 1;
    return 0;
}

int db_ap_update_field(const char *mac, const char *field, const char *value)
{
    if (!db || !db->root || !mac || !field) return -1;

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
        sys_err("db_ap_update_field: invalid field '%s'\n", field);
        return -1;
    }

    json_object *nodes = get_root_obj("nodes");
    if (!nodes) return -1;

    json_object *node = find_in_array(nodes, "mac", mac);
    if (!node) return -1;

    /* Check if it's an integer field */
    static const char *int_fields[] = {
        "online_user_num", "group_id", "device_down", "last_seen", NULL
    };
    int is_int = 0;
    for (int i = 0; int_fields[i]; i++) {
        if (strcmp(field, int_fields[i]) == 0) {
            is_int = 1;
            break;
        }
    }

    if (is_int) {
        set_int(node, field, atoi(value ? value : "0"));
    } else {
        set_str(node, field, value);
    }

    db->modified = 1;
    return 0;
}

int db_ap_get_field(const char *mac, const char *field, char *out, int outlen)
{
    if (!db || !db->root || !mac || !field || !out) return -1;

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
        sys_err("db_ap_get_field: invalid field '%s'\n", field);
        return -1;
    }

    json_object *nodes = get_root_obj("nodes");
    if (!nodes) return -1;

    json_object *node = find_in_array(nodes, "mac", mac);
    if (!node) return -1;

    json_object *jv;
    if (!json_object_object_get_ex(node, field, &jv)) {
        out[0] = '\0';
        return -1;
    }

    const char *val = json_object_get_string(jv);
    if (val) {
        strncpy(out, val, outlen - 1);
        out[outlen - 1] = '\0';
    } else {
        out[0] = '\0';
    }
    return 0;
}

int db_ap_set_offline(const char *mac)
{
    if (!db || !db->root || !mac) return -1;

    json_object *nodes = get_root_obj("nodes");
    if (!nodes) return -1;

    json_object *node = find_in_array(nodes, "mac", mac);
    if (!node) return -1;

    set_int(node,    "device_down", 1);
    set_int64(node,  "last_seen",   (int64_t)time(NULL));

    db->modified = 1;
    return 0;
}

/* ========================================================================
 * AP Group operations
 * ======================================================================== */

int db_group_create(const char *name, const char *description)
{
    if (!db || !db->root || !name) return -1;

    json_object *groups = get_or_create_array("ap_groups");

    /* Check for duplicate name */
    if (find_in_array(groups, "name", name))
        return -1;

    json_object *grp = json_object_new_object();
    json_object_object_add(grp, "id",           json_object_new_int(0));
    json_object_object_add(grp, "name",          json_object_new_string(name));
    json_object_object_add(grp, "description",   json_object_new_string(description ? description : ""));
    json_object_object_add(grp, "update_policy", json_object_new_string("manual"));
    json_object_object_add(grp, "created_at",    json_object_new_string(""));

    /* Assign next id */
    int max_id = 0;
    int len = json_object_array_length(groups);
    for (int i = 0; i < len; i++) {
        json_object *g = json_object_array_get_idx(groups, i);
        int gid = get_int(g, "id");
        if (gid > max_id) max_id = gid;
    }
    json_object_put(json_object_object_get(grp, "id"));
    json_object_object_add(grp, "id", json_object_new_int(max_id + 1));

    json_object_array_add(groups, grp);
    db->modified = 1;
    return 0;
}

int db_group_delete(int group_id)
{
    if (!db || !db->root) return -1;

    json_object *groups = get_root_obj("ap_groups");
    if (!groups) return -1;

    int idx = find_index_in_array_int(groups, "id", group_id);
    if (idx < 0) return -1;

    json_object_array_del_idx(groups, idx, 1);
    db->modified = 1;
    return 0;
}

int db_group_list(char *json_buf, int buflen)
{
    if (!db || !db->root || !json_buf) return -1;

    json_object *groups = get_root_obj("ap_groups");
    if (!groups) {
        snprintf(json_buf, buflen, "{\"groups\":[]}");
        return 0;
    }

    int pos = 0;
    int first = 1;

    if (buflen < 2) return -1;
    json_buf[pos++] = '{';

    int len = json_object_array_length(groups);
    for (int i = 0; i < len; i++) {
        json_object *g = json_object_array_get_idx(groups, i);

        if (pos >= buflen - 2) { json_buf[pos] = '\0'; return -1; }
        if (!first) json_buf[pos++] = ',';
        first = 0;

        int id   = get_int(g, "id");
        const char *name    = safe_str(g, "name");
        const char *desc    = safe_str(g, "description");
        const char *policy = safe_str(g, "update_policy");

        int n = snprintf(json_buf + pos, buflen - pos,
            "\"id\":%d,\"name\":\"%s\",\"description\":\"%s\",\"policy\":\"%s\"",
            id, name, desc, policy);
        if (n < 0 || n >= buflen - pos) { json_buf[pos] = '\0'; return -1; }
        pos += n;
    }

    if (pos >= buflen - 2) { json_buf[pos] = '\0'; return -1; }
    json_buf[pos++] = '}';
    json_buf[pos++] = ']';
    json_buf[pos++] = '}';
    json_buf[pos] = '\0';

    return pos;
}

int db_group_add_ap(const char *mac, int group_id)
{
    if (!db || !db->root || !mac) return -1;

    json_object *nodes = get_root_obj("nodes");
    if (!nodes) return -1;

    json_object *node = find_in_array(nodes, "mac", mac);
    if (!node) return -1;

    set_int(node, "group_id", group_id);
    db->modified = 1;
    return 0;
}

int db_group_remove_ap(const char *mac, int group_id)
{
    (void)group_id;
    return db_ap_update_field(mac, "group_id", "0");
}

/* ========================================================================
 * Alarm operations
 * ======================================================================== */

int db_alarm_insert(int level, const char *ap_mac, const char *message,
    const char *raw_data)
{
    if (!db || !db->root) return -1;

    json_object *alarms = get_or_create_array("alarm_events");

    json_object *al = json_object_new_object();
    json_object_object_add(al, "id",               json_object_new_int(0));
    json_object_object_add(al, "ap_mac",            json_object_new_string(ap_mac ? ap_mac : "unknown"));
    json_object_object_add(al, "alarm_rule_id",     json_object_new_int(0));
    json_object_object_add(al, "level",             json_object_new_int(level));
    json_object_object_add(al, "message",           json_object_new_string(message ? message : ""));
    json_object_object_add(al, "raw_data",          json_object_new_string(raw_data ? raw_data : ""));
    json_object_object_add(al, "created_at",        json_object_new_string(""));
    json_object_object_add(al, "acknowledged",     json_object_new_int(0));
    json_object_object_add(al, "acknowledged_by",   json_object_new_string(""));
    json_object_object_add(al, "acknowledged_at",   json_object_new_string(""));
    json_object_object_add(al, "resolved_at",       json_object_new_string(""));

    /* Assign next id */
    int max_id = 0;
    int len = json_object_array_length(alarms);
    for (int i = 0; i < len; i++) {
        json_object *a = json_object_array_get_idx(alarms, i);
        int aid = get_int(a, "id");
        if (aid > max_id) max_id = aid;
    }
    json_object_put(json_object_object_get(al, "id"));
    json_object_object_add(al, "id", json_object_new_int(max_id + 1));

    json_object_array_add(al, al);
    db->modified = 1;
    return 0;
}

int db_alarm_ack(int alarm_id, const char *acked_by)
{
    if (!db || !db->root) return -1;

    json_object *alarms = get_root_obj("alarm_events");
    if (!alarms) return -1;

    int len = json_object_array_length(alarms);
    for (int i = 0; i < len; i++) {
        json_object *al = json_object_array_get_idx(alarms, i);
        if (get_int(al, "id") == alarm_id) {
            set_int(al,  "acknowledged",     1);
            set_str(al,  "acknowledged_by",   acked_by ? acked_by : "system");

            char ts[64];
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);
            set_str(al, "acknowledged_at", ts);

            db->modified = 1;
            return 0;
        }
    }
    return -1;
}

int db_alarm_list(char *json_buf, int buflen, int limit)
{
    if (!db || !db->root || !json_buf) return -1;

    json_object *alarms = get_root_obj("alarm_events");
    if (!alarms) {
        snprintf(json_buf, buflen, "{\"alarms\":[]}");
        return 0;
    }

    int pos = 0;
    int first = 1;
    const char *level_str_map[] = { "info", "warn", "error", "critical" };

    if (buflen < 2) return -1;
    json_buf[pos++] = '{';

    const char *key = "\"alarms\":[";
    if (pos + (int)strlen(key) < buflen) {
        memcpy(json_buf + pos, key, strlen(key));
        pos += strlen(key);
    }

    int count = 0;
    int len = json_object_array_length(alarms);

    /* Iterate in reverse order (newest first) */
    for (int i = len - 1; i >= 0 && count < limit; i--) {
        json_object *al = json_object_array_get_idx(alarms, i);

        if (!first) {
            if (pos < buflen - 1) json_buf[pos++] = ',';
        }
        first = 0;

        int id     = get_int(al, "id");
        const char *mac  = safe_str(al, "ap_mac");
        int level  = get_int(al, "level");
        const char *msg = safe_str(al, "message");
        int ack    = get_int(al, "acknowledged");
        const char *ts   = safe_str(al, "created_at");

        const char *lstr = (level >= 0 && level <= 3)
            ? level_str_map[level] : "unknown";

        int n = snprintf(json_buf + pos, buflen - pos,
            "{\"id\":%d,\"mac\":\"%s\",\"level\":\"%s\","
            "\"message\":\"%s\",\"ack\":%d,\"ts\":\"%s\"}",
            id, mac, lstr, msg, ack, ts);
        if (n < 0 || n >= buflen - pos) { json_buf[pos] = '\0'; return -1; }
        pos += n;
        count++;
    }

    if (pos >= buflen - 2) { json_buf[pos] = '\0'; return -1; }
    json_buf[pos++] = ']';
    json_buf[pos++] = '}';
    json_buf[pos] = '\0';

    return pos;
}

int db_alarm_count_by_level(void)
{
    if (!db || !db->root) return 0;

    json_object *alarms = get_root_obj("alarm_events");
    if (!alarms) return 0;

    int count = 0;
    int len = json_object_array_length(alarms);
    for (int i = 0; i < len; i++) {
        json_object *al = json_object_array_get_idx(alarms, i);
        if (get_int(al, "acknowledged") == 0)
            count++;
    }
    return count;
}

/* ========================================================================
 * Firmware operations
 * ======================================================================== */

int db_firmware_insert(const char *version, const char *filename,
    uint32_t file_size, const char *sha256)
{
    if (!db || !db->root || !version) return -1;

    json_object *fws = get_or_create_array("firmwares");

    /* Check for duplicate version */
    if (find_in_array(fws, "version", version))
        return -1;

    json_object *fw = json_object_new_object();
    json_object_object_add(fw, "id",         json_object_new_int(0));
    json_object_object_add(fw, "version",     json_object_new_string(version));
    json_object_object_add(fw, "filename",     json_object_new_string(filename ? filename : ""));
    json_object_object_add(fw, "file_size",    json_object_new_int((int)file_size));
    json_object_object_add(fw, "sha256",       json_object_new_string(sha256 ? sha256 : ""));
    json_object_object_add(fw, "signature",   json_object_new_string(""));
    json_object_object_add(fw, "uploaded_at", json_object_new_string(""));
    json_object_object_add(fw, "uploaded_by", json_object_new_string(""));
    json_object_object_add(fw, "notes",       json_object_new_string(""));
    json_object_object_add(fw, "min_hw_version", json_object_new_string(""));

    /* Assign next id */
    int max_id = 0;
    int len = json_object_array_length(fws);
    for (int i = 0; i < len; i++) {
        json_object *f = json_object_array_get_idx(fws, i);
        int fid = get_int(f, "id");
        if (fid > max_id) max_id = fid;
    }
    json_object_put(json_object_object_get(fw, "id"));
    json_object_object_add(fw, "id", json_object_new_int(max_id + 1));

    json_object_array_add(fws, fw);
    db->modified = 1;
    return 0;
}

int db_firmware_list(char *json_buf, int buflen)
{
    if (!db || !db->root || !json_buf) return -1;

    json_object *fws = get_root_obj("firmwares");
    if (!fws) {
        snprintf(json_buf, buflen, "{\"firmwares\":[]}");
        return 0;
    }

    int pos = 0;
    int first = 1;

    if (buflen < 2) return -1;
    json_buf[pos++] = '{';

    const char *key = "\"firmwares\":[";
    if (pos + (int)strlen(key) < buflen) {
        memcpy(json_buf + pos, key, strlen(key));
        pos += strlen(key);
    }

    int len = json_object_array_length(fws);
    for (int i = len - 1; i >= 0; i--) {
        json_object *fw = json_object_array_get_idx(fws, i);

        if (!first) {
            if (pos < buflen - 1) json_buf[pos++] = ',';
        }
        first = 0;

        const char *ver = safe_str(fw, "version");
        const char *fn  = safe_str(fw, "filename");
        int sz    = get_int(fw, "file_size");
        const char *ts   = safe_str(fw, "uploaded_at");

        int n = snprintf(json_buf + pos, buflen - pos,
            "{\"version\":\"%s\",\"filename\":\"%s\","
            "\"size\":%d,\"uploaded_at\":\"%s\"}",
            ver, fn, sz, ts);
        if (n < 0 || n >= buflen - pos) { json_buf[pos] = '\0'; return -1; }
        pos += n;
    }

    if (pos >= buflen - 2) { json_buf[pos] = '\0'; return -1; }
    json_buf[pos++] = ']';
    json_buf[pos++] = '}';
    json_buf[pos] = '\0';

    return pos;
}

int db_firmware_getlatest(char *version_out, int version_len)
{
    if (!db || !db->root || !version_out) return -1;

    json_object *fws = get_root_obj("firmwares");
    if (!fws) return -1;

    int len = json_object_array_length(fws);
    if (len == 0) return -1;

    /* Last item = newest (append order) */
    json_object *fw = json_object_array_get_idx(fws, len - 1);
    const char *ver = safe_str(fw, "version");

    strncpy(version_out, ver, version_len - 1);
    version_out[version_len - 1] = '\0';
    return 0;
}

/* ========================================================================
 * Upgrade log
 * ======================================================================== */

int db_upgrade_start(const char *ap_mac, const char *from_ver, const char *to_ver)
{
    if (!db || !db->root || !ap_mac) return -1;

    json_object *logs = get_or_create_array("upgrade_logs");

    json_object *log = json_object_new_object();
    json_object_object_add(log, "id",           json_object_new_int(0));
    json_object_object_add(log, "ap_mac",       json_object_new_string(ap_mac));
    json_object_object_add(log, "from_version", json_object_new_string(from_ver ? from_ver : ""));
    json_object_object_add(log, "to_version",   json_object_new_string(to_ver ? to_ver : ""));
    json_object_object_add(log, "status",       json_object_new_string("pending"));
    json_object_object_add(log, "started_at",   json_object_new_string(""));
    json_object_object_add(log, "finished_at",  json_object_new_string(""));
    json_object_object_add(log, "error_message", json_object_new_string(""));

    /* Assign next id */
    int max_id = 0;
    int len = json_object_array_length(logs);
    for (int i = 0; i < len; i++) {
        json_object *l = json_object_array_get_idx(logs, i);
        int lid = get_int(l, "id");
        if (lid > max_id) max_id = lid;
    }
    json_object_put(json_object_object_get(log, "id"));
    json_object_object_add(log, "id", json_object_new_int(max_id + 1));

    json_object_array_add(logs, log);
    db->modified = 1;
    return 0;
}

int db_upgrade_finish(const char *ap_mac, const char *status, const char *error_msg)
{
    if (!db || !db->root || !ap_mac) return -1;

    json_object *logs = get_root_obj("upgrade_logs");
    if (!logs) return -1;

    int len = json_object_array_length(logs);
    for (int i = len - 1; i >= 0; i--) {
        json_object *log = json_object_array_get_idx(logs, i);
        const char *m = safe_str(log, "ap_mac");
        const char *s = safe_str(log, "status");
        if (strcmp(m, ap_mac) == 0 && strcmp(s, "pending") == 0) {
            set_str(log, "status",       status ? status : "unknown");
            set_str(log, "error_message", error_msg ? error_msg : "");
            set_str(log, "finished_at",  "");

            db->modified = 1;
            return 0;
        }
    }
    return -1;
}

int db_upgrade_progress(const char *ap_mac, int *status_out,
    char *from_ver, int from_len, char *to_ver, int to_len,
    char *error_msg, int err_len)
{
    if (!db || !db->root || !ap_mac) return -1;

    json_object *logs = get_root_obj("upgrade_logs");
    if (!logs) return -1;

    int len = json_object_array_length(logs);
    for (int i = len - 1; i >= 0; i--) {
        json_object *log = json_object_array_get_idx(logs, i);
        const char *m = safe_str(log, "ap_mac");
        if (strcmp(m, ap_mac) != 0) continue;

        if (status_out) {
            const char *s = safe_str(log, "status");
            if (strcmp(s, "success") == 0) *status_out = 1;
            else if (strcmp(s, "failed") == 0)  *status_out = 2;
            else                               *status_out = 0;
        }
        if (from_ver) strncpy(from_ver, safe_str(log, "from_version"), from_len - 1);
        if (to_ver)   strncpy(to_ver,   safe_str(log, "to_version"),   to_len   - 1);
        if (error_msg) strncpy(error_msg, safe_str(log, "error_message"), err_len - 1);
        if (from_ver && from_len > 0) from_ver[from_len - 1] = '\0';
        if (to_ver   && to_len   > 0) to_ver[to_len   - 1] = '\0';
        if (error_msg && err_len > 0) error_msg[err_len - 1] = '\0';
        return 0;
    }
    return -1;
}

/* ========================================================================
 * Audit log
 * ======================================================================== */

int db_audit_log(const char *user, const char *action,
    const char *resource_type, const char *resource_id,
    const char *old_value, const char *new_value,
    const char *ip_addr)
{
    if (!db || !db->root) return -1;

    json_object *logs = get_or_create_array("audit_logs");

    json_object *log = json_object_new_object();
    json_object_object_add(log, "id",           json_object_new_int(0));
    json_object_object_add(log, "user",          json_object_new_string(user ? user : "system"));
    json_object_object_add(log, "action",        json_object_new_string(action ? action : ""));
    json_object_object_add(log, "resource_type", json_object_new_string(resource_type ? resource_type : ""));
    json_object_object_add(log, "resource_id",   json_object_new_string(resource_id ? resource_id : ""));
    json_object_object_add(log, "old_value",     json_object_new_string(old_value ? old_value : ""));
    json_object_object_add(log, "new_value",     json_object_new_string(new_value ? new_value : ""));
    json_object_object_add(log, "ip_address",    json_object_new_string(ip_addr ? ip_addr : ""));
    json_object_object_add(log, "created_at",   json_object_new_string(""));

    /* Assign next id */
    int max_id = 0;
    int len = json_object_array_length(logs);
    for (int i = 0; i < len; i++) {
        json_object *l = json_object_array_get_idx(logs, i);
        int lid = get_int(l, "id");
        if (lid > max_id) max_id = lid;
    }
    json_object_put(json_object_object_get(log, "id"));
    json_object_object_add(log, "id", json_object_new_int(max_id + 1));

    json_object_array_add(logs, log);
    db->modified = 1;
    return 0;
}
