/*
 * ============================================================================
 *
 *       Filename:  db.c
 *
 *    Description:  JSON file-based database for AC Controller.
 *                  Replaces SQLite with zero external dependencies.
 *
 *        Version:  2.1
 *        Created:  2026-04-13
 *       Compiler:  gcc
 *
 * ============================================================================
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/file.h>
#include <unistd.h>
#include <json-c/json.h>

#include "db.h"
#include "resource.h"
#include "log.h"

/* Global database handle */
db_t *db = NULL;
struct tbl_col_t tables;

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
    fl.l_type = lock_type;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    return fcntl(fd, F_SETLKW, &fl);
}

/* Ensure database directory exists */
static int ensure_db_dir(void)
{
    const char *dir = "/etc/acctl";
    if (access(dir, F_OK) != 0) {
        if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
            set_error("Cannot create directory %s: %s", dir, strerror(errno));
            return -1;
        }
    }
    return 0;
}

/* Get or create a JSON object/array by path */
static json_object *json_get_path(json_object *root, const char *path, int create)
{
    char *p, *saveptr;
    char *path_copy = strdup(path);
    json_object *current = root;
    json_object *next = NULL;

    p = strtok_r(path_copy, "/", &saveptr);
    while (p && current) {
        if (!json_object_is_type(current, json_type_object)) {
            free(path_copy);
            return NULL;
        }

        if (!json_object_object_get_ex(current, p, &next)) {
            if (!create) {
                free(path_copy);
                return NULL;
            }
            /* Create next level */
            next = json_object_new_object();
            json_object_object_add(current, p, next);
        }
        current = next;
        p = strtok_r(NULL, "/", &saveptr);
    }

    free(path_copy);
    return current;
}

/* Get array from path, create if needed */
static json_object *json_get_array(json_object *root, const char *path, int create)
{
    json_object *obj = json_get_path(root, path, create);
    if (!obj && create) {
        obj = json_object_new_array();
        json_object_object_add(root, path, obj);
    }
    return obj;
}

/* ========================================================================
 * Database initialization and persistence
 * ======================================================================== */

int db_save(db_t *dbp)
{
    if (!dbp || !dbp->root)
        return -1;

    /* Create backup first */
    rename(DBNAME, DB_BACKUP);

    FILE *fp = fopen(DBNAME, "w");
    if (!fp) {
        set_error("Cannot open %s for writing: %s", DBNAME, strerror(errno));
        /* Restore backup */
        rename(DB_BACKUP, DBNAME);
        return -1;
    }

    /* Lock file for writing */
    int fd = fileno(fp);
    if (file_lock(fd, F_WRLCK) != 0) {
        set_error("Cannot lock database file");
        fclose(fp);
        return -1;
    }

    const char *json_str = json_object_to_json_string_ext(dbp->root,
        JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_SPACED);

    fprintf(fp, "%s\n", json_str);

    file_lock(fd, F_UNLCK);
    fclose(fp);

    dbp->modified = 0;
    sys_info("Database saved to %s\n", DBNAME);
    return 0;
}

static json_object *create_default_schema(void)
{
    json_object *root = json_object_new_object();

    /* resource - IP pool configuration */
    json_object *resource = json_object_new_object();
    json_object_object_add(resource, "ip_start", json_object_new_string(""));
    json_object_object_add(resource, "ip_end", json_object_new_string(""));
    json_object_object_add(resource, "ip_mask", json_object_new_string(""));
    json_object_object_add(root, "resource", resource);

    /* nodes - AP device array */
    json_object_object_add(root, "nodes", json_object_new_array());

    /* node_defaults - AP default config templates */
    json_object_object_add(root, "node_defaults", json_object_new_array());

    /* node_settings - AP pre-configuration */
    json_object_object_add(root, "node_settings", json_object_new_array());

    /* ap_groups - AP grouping */
    json_object_object_add(root, "ap_groups", json_object_new_array());

    /* alarm_events - Alarm/event log */
    json_object_object_add(root, "alarm_events", json_object_new_array());

    /* firmwares - Firmware repository */
    json_object_object_add(root, "firmwares", json_object_new_array());

    /* upgrade_logs - Upgrade tracking */
    json_object_object_add(root, "upgrade_logs", json_object_new_array());

    /* audit_logs - Audit log */
    json_object_object_add(root, "audit_logs", json_object_new_array());

    /* metadata */
    json_object *meta = json_object_new_object();
    json_object_object_add(meta, "version", json_object_new_string("2.1"));
    json_object_object_add(meta, "created_at", json_object_new_string(""));
    json_object_object_add(root, "_meta", meta);

    return root;
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
    FILE *fp = fopen(DBNAME, "r");
    if (fp) {
        /* Lock file for reading */
        int fd = fileno(fp);
        file_lock(fd, F_RDLCK);

        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (size > 0) {
            char *buf = malloc(size + 1);
            if (buf) {
                fread(buf, 1, size, fp);
                buf[size] = '\0';
                newdb->root = json_tokener_parse(buf);
                free(buf);
            }
        }

        file_lock(fd, F_UNLCK);
        fclose(fp);
    }

    /* Create default schema if load failed */
    if (!newdb->root) {
        newdb->root = create_default_schema();
        sys_info("Created new JSON database\n");
    }

    /* Initialize column cache for compatibility */
    tables.res.head = malloc(sizeof(struct col_name_t) * COLMAX);
    tables.res.col_num = 3;
    strncpy(tables.res.head[0].name, "ip_start", COLMAX-1);
    strncpy(tables.res.head[1].name, "ip_end", COLMAX-1);
    strncpy(tables.res.head[2].name, "ip_mask", COLMAX-1);

    if (dbp)
        *dbp = newdb;
    db = newdb;

    sys_info("JSON database initialized: %s\n", DBNAME);
    return 0;
}

void db_close(db_t *dbp)
{
    if (!dbp)
        dbp = db;
    if (!dbp)
        return;

    /* Save if modified */
    if (dbp->modified)
        db_save(dbp);

    if (dbp->root)
        json_object_put(dbp->root);

    free(dbp);
    if (db == dbp)
        db = NULL;
}

void db_tbl_col(db_t *dbp)
{
    /* Column info already cached in tables global */
    (void)dbp;
}
