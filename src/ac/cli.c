/*
 * ============================================================================
 *
 *       Filename:  cli.c
 *
 *    Description:  Lightweight CLI for LuCI web interface to access JSON DB.
 *                  Supports read and write operations needed by the web UI.
 *                  Not part of the acser daemon — standalone binary.
 *
 *        Version:  1.0
 *        Created:  2026-04-14
 *       Compiler:  gcc
 *
 * ============================================================================
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include <sys/types.h>

#define DB_PATH  "/etc/acctl/ac.json"
#define OUT_MAX  (64 * 1024)

static json_object *load_db(void)
{
    FILE *fp = fopen(DB_PATH, "r");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buf = malloc(size + 1);
    if (!buf) { fclose(fp); return NULL; }
    size = fread(buf, 1, size, fp);
    buf[size] = '\0';
    fclose(fp);

    json_object *obj = json_tokener_parse(buf);
    free(buf);
    return obj;
}

static int save_db(json_object *root)
{
    FILE *fp = fopen(DB_PATH, "w");
    if (!fp) return -1;
    const char *str = json_object_to_json_string_ext(root,
        JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_SPACED);
    fprintf(fp, "%s\n", str);
    fclose(fp);
    return 0;
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <command> [args]\n\n", prog);
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  groups              List all AP groups as JSON\n");
    fprintf(stderr, "  aps [--limit N]    List all APs as JSON\n");
    fprintf(stderr, "  alarms [--limit N]  List alarms as JSON\n");
    fprintf(stderr, "  firmware            List available firmware images as JSON\n");
    fprintf(stderr, "  stats               Database statistics\n");
    fprintf(stderr, "  ack <alarm_id>      Acknowledge an alarm\n");
    fprintf(stderr, "  ack-all             Acknowledge all alarms\n");
    fprintf(stderr, "  audit <user> <action> <rtype> <rid> <old> <new> <ip>  Write audit log\n");
}

/* Helper: get string from json object, returns "" if missing */
static const char *safe_get_str(json_object *obj, const char *key)
{
	json_object *jv;
	if (json_object_object_get_ex(obj, key, &jv) && jv)
		return json_object_get_string(jv);
	return "";
}

/* Helper: get int from json object, returns 0 if missing */
static int safe_get_int(json_object *obj, const char *key)
{
	json_object *jv;
	if (json_object_object_get_ex(obj, key, &jv) && jv)
		return json_object_get_int(jv);
	return 0;
}

/* Helper: get int as json string, returns "0" if missing */
static const char *safe_get_int_str(json_object *obj, const char *key)
{
	json_object *jv;
	if (json_object_object_get_ex(obj, key, &jv) && jv)
		return json_object_to_json_string(jv);
	return "0";
}

/* ---- groups ---- */
static int cmd_groups(void)
{
    json_object *root = load_db();
    if (!root) { fprintf(stderr, "Cannot open %s\n", DB_PATH); return 1; }

    json_object *groups;
    if (!json_object_object_get_ex(root, "ap_groups", &groups) ||
        !json_object_is_type(groups, json_type_array)) {
        printf("{\"groups\":[]}\n");
        json_object_put(root);
        return 0;
    }

    printf("{\"groups\":[");
    int len = json_object_array_length(groups);
    int i;
    for (i = 0; i < len; i++) {
        json_object *g = json_object_array_get_idx(groups, i);
        if (i > 0) printf(",");
        printf("{\"id\":%s,\"name\":\"%s\",\"description\":\"%s\",\"policy\":\"%s\"}",
            safe_get_int_str(g, "id"),
            safe_get_str(g, "name"),
            safe_get_str(g, "description"),
            safe_get_str(g, "update_policy"));
    }
    printf("]}\n");
    json_object_put(root);
    return 0;
}

/* ---- aps ---- */
static int cmd_aps(int limit)
{
    json_object *root = load_db();
    if (!root) { fprintf(stderr, "Cannot open %s\n", DB_PATH); return 1; }

    json_object *nodes;
    if (!json_object_object_get_ex(root, "nodes", &nodes) ||
        !json_object_is_type(nodes, json_type_array)) {
        printf("{\"aps\":[]}\n");
        json_object_put(root);
        return 0;
    }

    printf("{\"aps\":[");
    int len = json_object_array_length(nodes);
    int count = 0;
    int i;
    for (i = 0; i < len && count < limit; i++) {
        json_object *n = json_object_array_get_idx(nodes, i);
        if (i > 0) printf(",");
        printf("{\"mac\":\"%s\",\"hostname\":\"%s\",\"wan_ip\":\"%s\","
               "\"wifi_ssid\":\"%s\",\"firmware\":\"%s\","
               "\"online_users\":%s,\"device_down\":%s,\"last_seen\":%s,\"group_id\":%s}",
            safe_get_str(n, "mac"),
            safe_get_str(n, "hostname"),
            safe_get_str(n, "wan_ip"),
            safe_get_str(n, "wifi_ssid"),
            safe_get_str(n, "firmware"),
            safe_get_int_str(n, "online_user_num"),
            safe_get_int_str(n, "device_down"),
            safe_get_int_str(n, "last_seen"),
            safe_get_int_str(n, "group_id"));
        count++;
    }
    printf("]}\n");
    json_object_put(root);
    return 0;
}

/* ---- alarms ---- */
static int cmd_alarms(int limit)
{
    json_object *root = load_db();
    if (!root) { fprintf(stderr, "Cannot open %s\n", DB_PATH); return 1; }

    json_object *alarms;
    if (!json_object_object_get_ex(root, "alarm_events", &alarms) ||
        !json_object_is_type(alarms, json_type_array)) {
        printf("{\"alarms\":[]}\n");
        json_object_put(root);
        return 0;
    }

    const char *level_str[] = { "info", "warn", "error", "critical" };

    printf("{\"alarms\":[");
    int len = json_object_array_length(alarms);
    int count = 0;
    int i;
    for (i = len - 1; i >= 0 && count < limit; i--) {
        json_object *a = json_object_array_get_idx(alarms, i);
        if (i < len - 1) printf(",");
        int lvl = safe_get_int(a, "level");
        const char *lstr = (lvl >= 0 && lvl <= 3) ? level_str[lvl] : "unknown";
        printf("{\"id\":%s,\"mac\":\"%s\",\"level\":\"%s\","
               "\"message\":\"%s\",\"ack\":%s,\"ts\":\"%s\"}",
            safe_get_int_str(a, "id"),
            safe_get_str(a, "ap_mac"),
            lstr,
            safe_get_str(a, "message"),
            safe_get_int_str(a, "acknowledged"),
            safe_get_str(a, "created_at"));
        count++;
    }
    printf("]}\n");
    json_object_put(root);
    return 0;
}

/* ---- ack <id> ---- */
static int cmd_ack(const char *id_str)
{
    int alarm_id = atoi(id_str);

    json_object *root = load_db();
    if (!root) { fprintf(stderr, "Cannot open %s\n", DB_PATH); return 1; }

    json_object *alarms;
    if (!json_object_object_get_ex(root, "alarm_events", &alarms) ||
        !json_object_is_type(alarms, json_type_array)) {
        printf("{\"code\":1,\"message\":\"no alarms array\"}\n");
        json_object_put(root);
        return 1;
    }

    int len = json_object_array_length(alarms);
    for (int i = 0; i < len; i++) {
        json_object *a = json_object_array_get_idx(alarms, i);
        if (safe_get_int(a, "id") == alarm_id) {
            json_object_object_add(a, "acknowledged", json_object_new_int(1));
            json_object_object_add(a, "acknowledged_by", json_object_new_string("admin"));
            json_object_object_add(a, "acknowledged_at", json_object_new_string(""));
            save_db(root);
            printf("{\"code\":0,\"message\":\"ok\"}\n");
            json_object_put(root);
            return 0;
        }
    }

    printf("{\"code\":1,\"message\":\"alarm not found\"}\n");
    json_object_put(root);
    return 1;
}

/* ---- audit ---- */
static int cmd_audit(const char *user, const char *action,
    const char *rtype, const char *rid,
    const char *old_val, const char *new_val, const char *ip_addr)
{
    json_object *root = load_db();
    if (!root) { fprintf(stderr, "Cannot open %s\n", DB_PATH); return 1; }

    json_object *logs;
    if (!json_object_object_get_ex(root, "audit_logs", &logs)) {
        logs = json_object_new_array();
        json_object_object_add(root, "audit_logs", logs);
    }

    json_object *log = json_object_new_object();
    json_object_object_add(log, "id",            json_object_new_int(0));
    json_object_object_add(log, "user",           json_object_new_string(user    ? user    : "admin"));
    json_object_object_add(log, "action",         json_object_new_string(action  ? action  : ""));
    json_object_object_add(log, "resource_type",  json_object_new_string(rtype   ? rtype   : ""));
    json_object_object_add(log, "resource_id",    json_object_new_string(rid     ? rid     : ""));
    json_object_object_add(log, "old_value",      json_object_new_string(old_val ? old_val : ""));
    json_object_object_add(log, "new_value",      json_object_new_string(new_val ? new_val : ""));
    json_object_object_add(log, "ip_address",      json_object_new_string(ip_addr ? ip_addr : ""));
    json_object_object_add(log, "created_at",    json_object_new_string(""));

    /* Assign next id */
    int max_id = 0;
    int len = json_object_array_length(logs);
    for (int i = 0; i < len; i++) {
        json_object *l = json_object_array_get_idx(logs, i);
        int lid = safe_get_int(l, "id");
        if (lid > max_id) max_id = lid;
    }
    json_object_put(json_object_object_get(log, "id"));
    json_object_object_add(log, "id", json_object_new_int(max_id + 1));

    json_object_array_add(logs, log);
    save_db(root);
    printf("{\"code\":0}\n");
    json_object_put(root);
    return 0;
}

/* ---- ack-all ---- */
static int cmd_ack_all(void)
{
    json_object *root = load_db();
    if (!root) { fprintf(stderr, "Cannot open %s\n", DB_PATH); return 1; }

    json_object *alarms;
    if (!json_object_object_get_ex(root, "alarm_events", &alarms) ||
        !json_object_is_type(alarms, json_type_array)) {
        printf("{\"code\":0,\"acknowledged\":0}\n");
        json_object_put(root);
        return 0;
    }

    int len = json_object_array_length(alarms);
    int count = 0;
    int i;
    for (i = 0; i < len; i++) {
        json_object *a = json_object_array_get_idx(alarms, i);
        if (safe_get_int(a, "acknowledged") == 0) {
            json_object_object_add(a, "acknowledged", json_object_new_int(1));
            json_object_object_add(a, "acknowledged_by", json_object_new_string("admin"));
            json_object_object_add(a, "acknowledged_at", json_object_new_string(""));
            count++;
        }
    }

    if (count > 0)
        save_db(root);

    printf("{\"code\":0,\"acknowledged\":%d}\n", count);
    json_object_put(root);
    return 0;
}

/* ---- firmwares ---- */
static int cmd_firmwares(void)
{
    json_object *root = load_db();
    if (!root) { fprintf(stderr, "Cannot open %s\n", DB_PATH); return 1; }

    json_object *fws;
    if (!json_object_object_get_ex(root, "firmwares", &fws) ||
        !json_object_is_type(fws, json_type_array)) {
        printf("{\"firmwares\":[]}\n");
        json_object_put(root);
        return 0;
    }

    printf("{\"firmwares\":[");
    int len = json_object_array_length(fws);
    int i;
    for (i = len - 1; i >= 0; i--) {
        json_object *fw = json_object_array_get_idx(fws, i);
        if (i < len - 1) printf(",");
        printf("{\"version\":\"%s\",\"filename\":\"%s\","
               "\"size\":%d,\"sha256\":\"%s\",\"uploaded_at\":\"%s\"}",
            safe_get_str(fw, "version"),
            safe_get_str(fw, "filename"),
            safe_get_int(fw, "file_size"),
            safe_get_str(fw, "sha256"),
            safe_get_str(fw, "uploaded_at"));
    }
    printf("]}\n");
    json_object_put(root);
    return 0;
}

/* ---- stats ---- */
static int cmd_stats(void)
{
    json_object *root = load_db();
    if (!root) { printf("{\"ap_total\":0,\"ap_online\":0,\"alarms\":0,\"groups\":0}\n"); return 1; }

    int ap_total = 0, ap_online = 0, alarms = 0, groups = 0;

    json_object *nodes;
    if (json_object_object_get_ex(root, "nodes", &nodes) &&
        json_object_is_type(nodes, json_type_array)) {
        ap_total = json_object_array_length(nodes);
        int nlen = ap_total;
        for (int i = 0; i < nlen; i++) {
            json_object *n = json_object_array_get_idx(nodes, i);
            if (safe_get_int(n, "device_down") == 0)
                ap_online++;
        }
    }

    json_object *ev;
    if (json_object_object_get_ex(root, "alarm_events", &ev) &&
        json_object_is_type(ev, json_type_array)) {
        int alen = json_object_array_length(ev);
        for (int i = 0; i < alen; i++) {
            json_object *a = json_object_array_get_idx(ev, i);
            if (safe_get_int(a, "acknowledged") == 0)
                alarms++;
        }
    }

    json_object *grp;
    if (json_object_object_get_ex(root, "ap_groups", &grp) &&
        json_object_is_type(grp, json_type_array)) {
        groups = json_object_array_length(grp);
    }

    printf("{\"ap_total\":%d,\"ap_online\":%d,\"alarms\":%d,\"groups\":%d}\n",
        ap_total, ap_online, alarms, groups);
    json_object_put(root);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2) { print_usage(argv[0]); return 1; }

    const char *cmd = argv[1];

    if (strcmp(cmd, "groups") == 0) {
        return cmd_groups();
    } else if (strcmp(cmd, "aps") == 0) {
        int limit = 500;
        if (argc >= 3 && strcmp(argv[2], "--limit") == 0 && argc >= 4)
            limit = atoi(argv[3]);
        return cmd_aps(limit);
    } else if (strcmp(cmd, "alarms") == 0) {
        int limit = 50;
        if (argc >= 3 && strcmp(argv[2], "--limit") == 0 && argc >= 4)
            limit = atoi(argv[3]);
        return cmd_alarms(limit);
    } else if (strcmp(cmd, "firmware") == 0) {
        return cmd_firmwares();
    } else if (strcmp(cmd, "audit") == 0) {
        return cmd_audit(
            argc >= 3 ? argv[2] : NULL,
            argc >= 4 ? argv[3] : NULL,
            argc >= 5 ? argv[4] : NULL,
            argc >= 6 ? argv[5] : NULL,
            argc >= 7 ? argv[6] : NULL,
            argc >= 8 ? argv[7] : NULL,
            argc >= 9 ? argv[8] : NULL);
    } else if (strcmp(cmd, "ack") == 0) {
        if (argc < 3) { fprintf(stderr, "ack requires alarm_id\n"); return 1; }
        return cmd_ack(argv[2]);
    } else if (strcmp(cmd, "ack-all") == 0) {
        return cmd_ack_all();
    } else if (strcmp(cmd, "stats") == 0) {
        return cmd_stats();
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
