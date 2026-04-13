--[[
AC Controller — System Information
]]--

local sys = require "luci.sys"
local fs  = require "nixio.fs"

m = Map("acctl", translate("System Information"),
	translate("AC Controller runtime status and system information"))

-- Runtime status section
s = m:section(NamedSection, "acctl", "acctl",
	translate("Controller Status"))
s.addremove = false
s.anonymous = true

-- Is the AC server running?
local running = (sys.call("pgrep -x acser > /dev/null 2>&1") == 0)
local status_text = running
	and '<span style="color:#28a745;font-weight:bold">&#x25CF; Running</span>'
	or  '<span style="color:#dc3545;font-weight:bold">&#x25CB; Stopped</span>'

status = s:option(DummyValue, "_status", translate("Service Status"))
status.rawhtml = true
status.value = status_text

-- AC UUID
local uuid = sys.exec("cat /etc/acctl/ac.uuid 2>/dev/null")
if uuid == "" then uuid = "N/A" end
uuid_opt = s:option(DummyValue, "_uuid", translate("AC UUID"))
uuid_opt.value = uuid

-- System uptime
local uptime = sys.exec("cat /proc/uptime 2>/dev/null")
if uptime ~= "" then
	uptime = string.format("%.1f hours", tonumber(uptime:match("^[%.%d]+")) / 3600)
else
	uptime = "N/A"
end
uptime_opt = s:option(DummyValue, "_uptime", translate("System Uptime"))
uptime_opt.value = uptime

-- Database statistics section
s2 = m:section(NamedSection, "acctl", "acctl",
	translate("Database Statistics"))
s2.addremove = false
s2.anonymous = true

local db_path = "/etc/acctl/ac.db"
local db_size = "N/A"
if fs.access(db_path) then
	local sz = tonumber(fs.stat(db_path, "size"))
	if sz then
		if sz > 1048576 then
			db_size = string.format("%.1f MB", sz / 1048576)
		elseif sz > 1024 then
			db_size = string.format("%.1f KB", sz / 1024)
		else
			db_size = string.format("%d B", sz)
		end
	end
end

local ap_total    = tonumber(sys.exec(
	"sqlite3 /etc/acctl/ac.db 'SELECT COUNT(*) FROM node' 2>/dev/null")) or 0
local ap_online   = tonumber(sys.exec(
	"sqlite3 /etc/acctl/ac.db 'SELECT COUNT(*) FROM node WHERE device_down=0' 2>/dev/null")) or 0
local alarm_count = tonumber(sys.exec(
	"sqlite3 /etc/acctl/ac.db 'SELECT COUNT(*) FROM alarm_event WHERE acknowledged=0' 2>/dev/null")) or 0
local group_count = tonumber(sys.exec(
	"sqlite3 /etc/acctl/ac.db 'SELECT COUNT(*) FROM ap_group' 2>/dev/null")) or 0

db_size_opt = s2:option(DummyValue, "_db_size", translate("Database Size"))
db_size_opt.value = db_size

ap_total_opt = s2:option(DummyValue, "_ap_total", translate("Total APs"))
ap_total_opt.value = tostring(ap_total)

ap_online_opt = s2:option(DummyValue, "_ap_online", translate("Online APs"))
ap_online_opt.value = tostring(ap_online)

alarm_opt = s2:option(DummyValue, "_alarms", translate("Active Alarms"))
alarm_opt.value = tostring(alarm_count)

group_opt = s2:option(DummyValue, "_groups", translate("AP Groups"))
group_opt.value = tostring(group_count)

-- Service control
s3 = m:section(NamedSection, "acctl", "acctl",
	translate("Service Control"))
s3.addremove = false
s3.anonymous = true

restart_btn = s3:option(Button, "_restart", translate("Restart Controller"))
restart_btn.inputstyle = "apply"
function restart_btn.write(self, section)
	sys.exec("/etc/init.d/acctl restart > /dev/null 2>&1")
end

return m
