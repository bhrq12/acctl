/*
 * =====================================================================================
 *
 *       Filename:  sql.h
 *
 *    Description:  SQLite → JSON database migration compatibility layer.
 *                  All SQL API calls are redirected to the new JSON db.h backend.
 *                  Once migration is verified, callers should be updated to use
 *                  db.h directly and this file should be deleted.
 *
 *        Version:  3.0
 *        Created:  2026-04-14
 *       Revision:  compatibility shim for sql_* → db_* migration
 *       Compiler:  gcc
 *
 * =====================================================================================
 */
#ifndef __SQL_H__
#define __SQL_H__

/*
 * Redirect all sqlite3 types and macros to our JSON db types.
 * This keeps old #include <sqlite3.h> code compiling without modification.
 */
#include <stdint.h>
#include "db.h"

/*
 * For backward compatibility with code that treats 'SQL' as a pointer type.
 * We use db_t* internally, so we just alias it.
 * Callers that currently use 'sql' as a global sqlite3* will now use
 * the global db_t* db declared in db.c.
 */
#define SQL     db_t *
#define SQL_NULL  DB_NULL

/*
 * pr_sqlerr() was:  sys_err("SQLite error: %s\n", sqlite3_errmsg(sql))
 * Now:              sys_err("DB error: %s\n", db_last_error())
 */
#define pr_sqlerr()  pr_dberr()

/* ========================================================================
 * Backward-compatible macros from the original sql.h (used by callers)
 * ======================================================================== */

/*
 * These macros were in the original sql.h JSON_ENCODE_* helpers.
 * We keep them here so sql.c → db.c is a pure function rename.
 */
#define GETRES  "resource"

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

/* ========================================================================
 * Forward-compatible API — callers should migrate to db_* functions
 * New code should #include "db.h" directly instead of "sql.h"
 * ======================================================================== */

/*
 * Global handle — used by old callers that did:
 *   extern sqlite3 *sql;
 *   if (sql_init(sql) != 0) { ... }
 * Now maps to:
 *   extern db_t *db;
 *   if (db_init(&db) != 0) { ... }
 */
extern db_t *db;
extern struct tbl_col_t tables;

/* json_attrs for mjson parser — exported for resource.c */
extern const char *json_attrs[];

/* ---- Initialization ---- */
int  db_init(db_t **dbp);
void db_close(db_t *db);

/* Old API names still accepted during migration */
#define sql_init(dbp)        db_init(dbp)
#define sql_close(dbp)       db_close(dbp)
#define sql_tbl_col(dbp)     db_tbl_col(dbp)
#define sql_query_res(dbp,b,l) db_query_res(dbp,b,l)

/* ---- AP operations ---- */
#define sql_ap_upsert(a,b,c,d,e,f,g)     db_ap_upsert(a,b,c,d,e,f,g)
#define sql_ap_update_field(a,b,c)        db_ap_update_field(a,b,c)
#define sql_ap_get_field(a,b,c,d)        db_ap_get_field(a,b,c,d)
#define sql_ap_set_offline(a)            db_ap_set_offline(a)

/* ---- AP Group operations ---- */
#define sql_group_create(a,b)             db_group_create(a,b)
#define sql_group_delete(a)              db_group_delete(a)
#define sql_group_list(a,b)              db_group_list(a,b)
#define sql_group_add_ap(a,b)            db_group_add_ap(a,b)
#define sql_group_remove_ap(a,b)         db_group_remove_ap(a,b)

/* ---- Alarm operations ---- */
#define sql_alarm_insert(a,b,c,d)        db_alarm_insert(a,b,c,d)
#define sql_alarm_ack(a,b)               db_alarm_ack(a,b)
#define sql_alarm_list(a,b,c)            db_alarm_list(a,b,c)
#define sql_alarm_count_by_level()       db_alarm_count_by_level()

/* ---- Firmware operations ---- */
#define sql_firmware_insert(a,b,c,d)      db_firmware_insert(a,b,c,d)
#define sql_firmware_list(a,b)           db_firmware_list(a,b)
#define sql_firmware_getlatest(a,b)      db_firmware_getlatest(a,b)

/* ---- Upgrade log ---- */
#define sql_upgrade_start(a,b,c)          db_upgrade_start(a,b,c)
#define sql_upgrade_finish(a,b,c)         db_upgrade_finish(a,b,c)
#define sql_upgrade_progress(a,b,c,d,e,f,g,h) \
	                                      db_upgrade_progress(a,b,c,d,e,f,g,h)

/* ---- Audit log ---- */
#define sql_audit_log(a,b,c,d,e,f,g)      db_audit_log(a,b,c,d,e,f,g)

#endif /* __SQL_H__ */
