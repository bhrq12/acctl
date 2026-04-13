--[[
AC Controller — Alarm Center (JSON backend)
]]

local sys       = require "luci.sys"
local util      = require "luci.util"
local uci       = require "luci.model.uci".cursor()
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
function get_alarms()
	local alarms = {}
	local output = sys.exec("acctl-cli alarms --limit 100 2>/dev/null")
	local ok, data = pcall(luci_http.parse_json, output)
	if not ok or not data or not data.alarms then
		return alarms
	end
	for _, a in ipairs(data.alarms) do
		table.insert(alarms, {
			id            = tonumber(a.id) or 0,
			ap_mac        = a.mac or "",
			level         = tonumber(a.level_num) or 0,
			message       = a.message or "",
			acknowledged  = a.acknowledged or false,
			created_at    = a.ts or ""
		})
	end
	return alarms
end

s2 = m:section(Table, get_alarms(),
	translatef("Active Alarms (%d)", #(get_alarms())))

s2:option(DummyValue, "id",         translate("ID"))
s2:option(DummyValue, "ap_mac",     translate("AP MAC"))

level_col = s2:option(DummyValue, "level", translate("Level"))
level_col.template = "acctl/alarm_level"

s2:option(DummyValue, "message",     translate("Message"))
s2:option(DummyValue, "created_at",  translate("Time"))

-- Acknowledge all
function m:parse()
	Map.parse(self)
	if luci_http.formvalue("ack_all") then
		sys.exec("acctl-cli ack-all 2>/dev/null")
	end
end

return m
