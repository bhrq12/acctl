--[[
AC Controller — Alarm Center
]]--

local sys = require "luci.sys"
local util = require "luci.util"
local uci = require "luci.model.uci".cursor()
local luci_http = require "luci.http"

m = Map("acctl", translate("Alarm Center"),
	translate("View and manage system alarms"))

-- Alarm rules section
s = m:section(TypedSection, "alarm", translate("Alarm Rules"),
	translate("Configure alarm trigger conditions"))
s.anonymous = true
s.addremove = true

s:option(Value, "name", translate("Rule Name")).rmempty = false

level = s:option(ListValue, "level", translate("Severity Level"))
level:value("0", translate("Info"))
level:value("1", translate("Warning"))
level:value("2", translate("Error"))
level:value("3", translate("Critical"))
level.default = "1"

s:option(Value, "threshold", translate("Threshold"))
s:option(Value, "window", translate("Time Window (seconds)"))
s:option(Value, "cooldown", translate("Cooldown (seconds)"))

notify = s:option(Flag, "notify", translate("Enable Notification"))
notify.default = "1"

-- Active alarms table
s2 = m:section(Table, {},
	translatef("Active Alarms"))

function get_alarms()
	local alarms = {}
	local cmd = "sqlite3 /etc/acctl/ac.db " ..
		"'SELECT id,ap_mac,level,message,acknowledged,created_at " ..
		"FROM alarm_event WHERE acknowledged=0 ORDER BY created_at DESC LIMIT 100' " ..
		"2>/dev/null"
	local output = sys.exec(cmd)
	for row in output:gmatch("[^\r\n]+") do
		local fields = util.split(row, "|")
		if #fields >= 6 then
			table.insert(alarms, {
				id = tonumber(fields[1]) or 0,
				ap_mac = fields[2] or "",
				level = tonumber(fields[3]) or 0,
				message = fields[4] or "",
				acknowledged = (tonumber(fields[5]) == 1),
				created_at = fields[6] or ""
			})
		end
	end
	return alarms
end

local alarms = get_alarms()
s2 = m:section(Table, alarms,
	translatef("Active Alarms (%d)", #alarms))

id_col = s2:option(DummyValue, "id", translate("ID"))
mac_col = s2:option(DummyValue, "ap_mac", translate("AP MAC"))

level_col = s2:option(DummyValue, "level", translate("Level"))
level_col.template = "acctl/alarm_level"

s2:option(DummyValue, "message", translate("Message"))
s2:option(DummyValue, "created_at", translate("Time"))

-- Acknowledge action
function m:parse()
	Map.parse(self)
	if luci_http.formvalue("ack_all") then
		sys.exec("sqlite3 /etc/acctl/ac.db " ..
			"'UPDATE alarm_event SET acknowledged=1," ..
			"acknowledged_by=\"admin\"," ..
			"acknowledged_at=datetime(\"now\") " ..
			"WHERE acknowledged=0' 2>/dev/null")
	end
end

return m
