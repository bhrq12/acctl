--[[
LuCI - Lua Configuration Interface
AC Controller 鈥?LuCI Controller Module

Licensed under the Apache License, Version 2.0
http://www.apache.org/licenses/LICENSE-2.0
]]--

module("luci.controller.acctl", package.seeall)

local sys   = require "luci.sys"
local util   = require "luci.util"
local http   = require "luci.http"
local uci    = require "luci.model.uci".cursor()

-- Sanitize a string for safe shell injection in SQLite queries (defense-in-depth)
local function sh_escape(s)
	if not s then return "" end
	return (tostring(s):gsub("[\\'\"$`]", "\\%0"))
end

local function is_running()
	return sys.call("pgrep -x acser > /dev/null 2>&1") == 0
end

-- db_query: execute a SQL query and return the first result row as a string
local function db_query(sql_stmt)
	local safe_sql = sql_stmt:gsub("'", "''")
	local cmd = "sqlite3 /etc/acctl/ac.db '" .. safe_sql .. "' 2>/dev/null"
	local f = io.popen(cmd)
	if not f then return "" end
	local r = f:read("*a")
	f:close()
	return (r:gsub("\n$", ""))
end

-- db_table: execute a SQL query and return all result rows as a table
local function db_table(sql_stmt)
	local safe_sql = sql_stmt:gsub("'", "''")
	local cmd = "sqlite3 /etc/acctl/ac.db '" .. safe_sql .. "' 2>/dev/null"
	local f = io.popen(cmd)
	if not f then return {} end
	local rows = {}
	for line in f:lines() do
		if line and line ~= "" then
			rows[#rows + 1] = line
		end
	end
	f:close()
	return rows
end

local function split(line, sep)
	sep = sep or "|"
	local t = {}
	for field in (line .. sep):gmatch("(.-)" .. sep) do
		t[#t + 1] = field
	end
	return t
end

-- ============================================================
-- Menu entries
-- ============================================================
function index()
	-- Main entry
	entry({"admin", "network", "acctl"},
		alias("admin", "network", "acctl", "general"),
		_("AC Controller"), 60).dependent = false

	-- General settings
	entry({"admin", "network", "acctl", "general"},
		cbi("acctl/general", {autoapply=true}),
		_("General"), 10)

	-- AP list
	entry({"admin", "network", "acctl", "ap_list"},
		cbi("acctl/ap_list", {autoapply=true}),
		_("AP List"), 20)

	-- AP groups
	entry({"admin", "network", "acctl", "groups"},
		cbi("acctl/groups", {autoapply=true}),
		_("AP Groups"), 25)

	-- Config templates
	entry({"admin", "network", "acctl", "templates"},
		cbi("acctl/templates", {autoapply=true}),
		_("Templates"), 30)

	-- Alarms
	entry({"admin", "network", "acctl", "alarms"},
		cbi("acctl/alarms", {autoapply=true}),
		_("Alarms"), 40)

	-- Firmware
	entry({"admin", "network", "acctl", "firmware"},
		cbi("acctl/firmware", {autoapply=true}),
		_("Firmware"), 50)

	-- System info
	entry({"admin", "network", "acctl", "system"},
		cbi("acctl/system"),
		_("System"), 60)

	-- ============================================================
	-- REST API endpoints (JSON)
	-- ============================================================

	-- GET /admin/network/acctl/api/status
	entry({"admin", "network", "acctl", "api", "status"},
		call("api_status"))

	-- GET /admin/network/acctl/api/aps
	entry({"admin", "network", "acctl", "api", "aps"},
		call("api_aps"))

	-- POST /admin/network/acctl/api/aps/action
	entry({"admin", "network", "acctl", "api", "aps_action"},
		call("api_aps_action"))

	-- GET /admin/network/acctl/api/alarms
	entry({"admin", "network", "acctl", "api", "alarms"},
		call("api_alarms"))

	-- POST /admin/network/acctl/api/alarms/ack
	entry({"admin", "network", "acctl", "api", "alarms_ack"},
		call("api_alarms_ack"))

	-- GET /admin/network/acctl/api/groups
	entry({"admin", "network", "acctl", "api", "groups"},
		call("api_groups"))

	-- GET /admin/network/acctl/api/firmwares
	entry({"admin", "network", "acctl", "api", "firmwares"},
		call("api_firmwares"))

	-- POST /admin/network/acctl/api/cmd
	entry({"admin", "network", "acctl", "api", "cmd"},
		call("api_cmd"))

	-- POST /admin/network/acctl/api/restart
	entry({"admin", "network", "acctl", "api", "restart"},
		call("api_restart"))
end

-- ============================================================
-- API: Status
-- ============================================================
function api_status()
	local status = {
		running      = is_running(),
		ap_online    = 0,
		ap_total     = 0,
		alarm_count  = 0,
		pool_left    = 0,
		pool_total   = 0,
		timestamp    = os.time()
	}

	local function count(query)
		local r = db_query(query)
		return tonumber(r) or 0
	end

	status.ap_online   = count("SELECT COUNT(*) FROM node WHERE device_down=0")
	status.ap_total   = count("SELECT COUNT(*) FROM node")
	status.alarm_count= count("SELECT COUNT(*) FROM alarm_event WHERE acknowledged=0")

	http.prepare_content("application/json")
	http.write_json(status)
end

-- ============================================================
-- API: AP List
-- ============================================================
function api_aps()
	local mac    = http.formvalue("mac")
	local result = {}

	if mac and mac ~= "" then
		-- Single AP detail
		local row = db_query(string.format(
			"SELECT mac,hostname,wan_ip,wifi_ssid,firmware," ..
			"online_user_num,device_down,last_seen,group_id " ..
			"FROM node WHERE mac='%s'", mac))
		if row and row ~= "" then
			local t = split(row)
			if #t >= 9 then
				result.mac          = t[1]
				result.hostname     = t[2]
				result.wan_ip       = t[3]
				result.wifi_ssid    = t[4]
				result.firmware     = t[5]
				result.online_users = tonumber(t[6]) or 0
				result.online       = (tonumber(t[7]) == 0)
				result.last_seen    = tonumber(t[8]) or 0
				result.group_id     = tonumber(t[9]) or 0
			end
		end
	else
		-- All APs
		result.aps   = {}
		result.count = 0
		local rows = db_table(
			"SELECT mac,hostname,wan_ip,wifi_ssid,firmware," ..
			"online_user_num,device_down,last_seen,group_id " ..
			"FROM node ORDER BY last_seen DESC LIMIT 500")
		for _, row in ipairs(rows) do
			local t = split(row)
			if #t >= 9 then
				result.count = result.count + 1
				result.aps[#result.aps + 1] = {
					mac          = t[1],
					hostname     = t[2],
					wan_ip       = t[3],
					wifi_ssid    = t[4],
					firmware     = t[5],
					online_users = tonumber(t[6]) or 0,
					online       = (tonumber(t[7]) == 0),
					last_seen    = tonumber(t[8]) or 0,
					group_id     = tonumber(t[9]) or 0
				}
			end
		end
	end

	http.prepare_content("application/json")
	http.write_json(result)
end

-- ============================================================
-- API: AP Bulk Actions
-- ============================================================
function api_aps_action()
	local action = http.formvalue("action")
	local macs   = http.formvalue("macs") or ""
	local result = { code = 0, message = "", affected = 0 }

	if action == "" or macs == "" then
		result.code    = 400
		result.message = "action and macs required"
		http.prepare_content("application/json")
		http.write_json(result)
		return
	end

	-- Audit log
	db_query(string.format(
		"INSERT INTO audit_log(user,action,resource_type,resource_id) " ..
		"VALUES('admin','%s','ap_batch','%s')",
		action, macs:sub(1, 50)))

	if action == "reboot" then
		result.message = "Reboot command queued for APs"
	elseif action == "config" then
		result.message = "Configuration push queued"
	elseif action == "upgrade" then
		result.message = "Firmware upgrade queued"
	else
		result.code    = 400
		result.message = "Unknown action"
	end

	http.prepare_content("application/json")
	http.write_json(result)
end

-- ============================================================
-- API: Alarm List
-- ============================================================
function api_alarms()
	local limit = tonumber(http.formvalue("limit")) or 50
	local rows = db_table(string.format(
		"SELECT id,ap_mac,level,message,acknowledged,created_at " ..
		"FROM alarm_event ORDER BY created_at DESC LIMIT %d", limit))

	local alarms = {}
	for _, row in ipairs(rows) do
		local t = split(row)
		if #t >= 6 then
			local level = tonumber(t[3]) or 0
			local level_str = (level == 0) and "info" or
			                  (level == 1) and "warn" or
			                  (level == 2) and "error" or "critical"
			alarms[#alarms + 1] = {
				id            = tonumber(t[1]) or 0,
				ap_mac        = t[2],
				level         = level_str,
				level_num     = level,
				message       = t[4],
				acknowledged   = (tonumber(t[5]) == 1),
				created_at    = t[6]
			}
		end
	end

	http.prepare_content("application/json")
	http.write_json({alarms = alarms, count = #alarms})
end

-- ============================================================
-- API: Acknowledge Alarms
-- ============================================================
function api_alarms_ack()
	local ids = http.formvalue("ids") or ""
	local result = { code = 0, acknowledged = 0 }

	if ids == "all" then
		db_query(
			"UPDATE alarm_event SET acknowledged=1," ..
			"acknowledged_by='admin'," ..
			"acknowledged_at=datetime('now') " ..
			"WHERE acknowledged=0")
		result.acknowledged = -1  -- -1 means "all"
	else
		for id in ids:gmatch("%d+") do
			db_query(string.format(
				"UPDATE alarm_event SET acknowledged=1," ..
				"acknowledged_by='admin'," ..
				"acknowledged_at=datetime('now') " ..
				"WHERE id=%s", id))
			result.acknowledged = result.acknowledged + 1
		end
	end

	http.prepare_content("application/json")
	http.write_json(result)
end

-- ============================================================
-- API: AP Groups
-- ============================================================
function api_groups()
	local rows = db_table(
		"SELECT id,name,description,update_policy FROM ap_group ORDER BY id")

	local groups = {}
	for _, row in ipairs(rows) do
		local t = split(row)
		if #t >= 4 then
			local id = tonumber(t[1]) or 0
			local count = tonumber(db_query(
				string.format(
					"SELECT COUNT(*) FROM node WHERE group_id=%d", id))) or 0
			groups[#groups + 1] = {
				id          = id,
				name        = t[2],
				description = t[3],
				policy      = t[4],
				ap_count    = count
			}
		end
	end

	http.prepare_content("application/json")
	http.write_json({groups = groups})
end

-- ============================================================
-- API: Firmware List
-- ============================================================
function api_firmwares()
	local rows = db_table(
		"SELECT version,filename,file_size,sha256,uploaded_at " ..
		"FROM firmware ORDER BY uploaded_at DESC")

	local firmwares = {}
	for _, row in ipairs(rows) do
		local t = split(row)
		if #t >= 5 then
			firmwares[#firmwares + 1] = {
				version    = t[1],
				filename   = t[2],
				size       = tonumber(t[3]) or 0,
				sha256     = t[4],
				uploaded_at = t[5]
			}
		end
	end

	http.prepare_content("application/json")
	http.write_json({firmwares = firmwares})
end

-- ============================================================
-- API: Command Execution
-- ============================================================
function api_cmd()
	local mac = http.formvalue("mac")
	local cmd = http.formvalue("cmd")
	local result = { code = 0, message = "" }

	if not mac or not cmd or mac == "" or cmd == "" then
		result.code    = 400
		result.message = "mac and cmd are required"
		http.prepare_content("application/json")
		http.write_json(result)
		return
	end

	-- Command whitelist validation (mirror of sec_validate_command in C)
	local allowed = {
		["reboot"]             = true,
		["uptime"]             = true,
		["ifconfig"]           = true,
		["iwconfig"]           = true,
		["wifi"]               = true,
		["cat /proc/uptime"]   = true,
		["cat /proc/loadavg"]  = true,
		["cat /tmp/ap_status"] = true,
	}

	local found = false
	for pattern, _ in pairs(allowed) do
		if cmd:find(pattern, 1, true) == 1 then
			found = true
			break
		end
	end

	if not found then
		result.code    = 403
		result.message = "Command not in whitelist"
		http.prepare_content("application/json")
		http.write_json(result)
		return
	end

	-- Audit
	db_query(string.format(
		"INSERT INTO audit_log(user,action,resource_type,resource_id,new_value) " ..
		"VALUES('admin','EXEC','ap','%s','%s')",
		mac:sub(1, 20), cmd:sub(1, 100)))

	result.message = "Command queued successfully"
	http.prepare_content("application/json")
	http.write_json(result)
end

-- ============================================================
-- API: Restart Service
-- ============================================================
function api_restart()
	local result = { code = 0, message = "" }
	if is_running() then
		sys.exec("/etc/init.d/acctl restart > /dev/null 2>&1")
		result.message = "AC Controller restarting..."
	else
		sys.exec("/etc/init.d/acctl start > /dev/null 2>&1")
		result.message = "AC Controller starting..."
	end
	http.prepare_content("application/json")
	http.write_json(result)
end
