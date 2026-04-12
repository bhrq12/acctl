--[[
AC Controller — AP List Management
]]--

local sys = require "luci.sys"
local util = require "luci.util"
local luci_http = require "luci.http"

m = Map("acctl", translate("AP List"),
	translate("Manage connected Access Points"))

-- AP table
local all_aps = {}
local cmd = "sqlite3 /etc/acctl/ac.db " ..
	"'SELECT mac,hostname,wan_ip,wifi_ssid,firmware," ..
	"online_user_num,device_down,last_seen,group_id " ..
	"FROM node ORDER BY last_seen DESC' 2>/dev/null"
local output = sys.exec(cmd)

for row in output:gmatch("[^\r\n]+") do
	local fields = util.split(row, "|")
	if #fields >= 9 then
		local t = {
			mac = fields[1],
			hostname = fields[2] or "",
			wan_ip = fields[3] or "",
			wifi_ssid = fields[4] or "",
			firmware = fields[5] or "",
			online_users = tonumber(fields[6]) or 0,
			device_down = tonumber(fields[7]) or 1,
			last_seen = tonumber(fields[8]) or 0,
			group_id = tonumber(fields[9]) or 0
		}
		-- Convert timestamp to readable format
		if t.last_seen > 0 then
			t.last_seen_str = os.date("%Y-%m-%d %H:%M:%S", t.last_seen)
		else
			t.last_seen_str = "Never"
		end
		table.insert(all_aps, t)
	end
end

s = m:section(Table, all_aps,
	translatef("Access Points (%d total)", #all_aps))

s:option(DummyValue, "mac", translate("MAC Address"))
s:option(DummyValue, "hostname", translate("Hostname"))

status_col = s:option(DummyValue, "device_down", translate("Status"))
status_col.template = "acctl/ap_status"

s:option(DummyValue, "wan_ip", translate("WAN IP"))
s:option(DummyValue, "wifi_ssid", translate("SSID"))
s:option(DummyValue, "firmware", translate("Firmware"))

users_col = s:option(DummyValue, "online_users", translate("Users"))
users_col.template = "acctl/ap_users"

s:option(DummyValue, "last_seen_str", translate("Last Seen"))

-- Action buttons
function s.render_footer(self)
	-- Refresh button
	luci_http.write('<div class="cbi-page-actions">')
	luci_http.write('<input class="cbi-button cbi-button-action important" ' ..
		'type="submit" name="refresh" value="' .. translate("Refresh") .. '" />')
	luci_http.write('</div>')
end

-- Bulk actions section
s2 = m:section(NamedSection, "acctl", "acctl",
	translate("Bulk Operations"))
s2.addremove = false

local sel = s:option(Value, "_selected", translate("Selected APs (comma-separated MACs)"))
sel.placeholder = "aa:bb:cc:dd:ee:ff, 11:22:33:44:55:66"
sel.datatype = "string"

local action = s2:option(ListValue, "_action", translate("Action"))
action:value("none", translate("Select action..."))
action:value("reboot", translate("Reboot APs"))
action:value("upgrade", translate("Upgrade Firmware"))
action:value("config", translate("Push Configuration"))
action:value("offline", translate("Mark Offline"))

apply_btn = s2:option(Button, "_apply", translate("Apply"))
apply_btn.inputstyle = "apply"

return m
