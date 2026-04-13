--[[
AC Controller — AP List Management (JSON backend)
]]

local sys       = require "luci.sys"
local util      = require "luci.util"
local luci_http = require "luci.http"

m = Map("acctl", translate("AP List"),
	translate("Manage connected Access Points"))

-- Load APs from CLI
local function get_aps()
	local aps = {}
	local output = sys.exec("acctl-cli aps --limit 500 2>/dev/null")
	local ok, data = pcall(luci_http.parse_json, output)
	if not ok or not data or not data.aps then
		return aps
	end
	for _, t in ipairs(data.aps) do
		local last_seen = tonumber(t.last_seen) or 0
		local last_seen_str = "Never"
		if last_seen > 0 then
			last_seen_str = os.date("%Y-%m-%d %H:%M:%S", last_seen)
		end
		table.insert(aps, {
			mac            = t.mac or "",
			hostname       = t.hostname or "",
			wan_ip         = t.wan_ip or "",
			wifi_ssid      = t.wifi_ssid or "",
			firmware       = t.firmware or "",
			online_users   = tonumber(t.online_users) or 0,
			device_down    = tonumber(t.device_down) or 1,
			last_seen      = last_seen,
			last_seen_str  = last_seen_str,
			group_id       = tonumber(t.group_id) or 0
		})
	end
	return aps
end

local all_aps = get_aps()
s = m:section(Table, all_aps,
	translatef("Access Points (%d total)", #all_aps))

s:option(DummyValue, "mac", translate("MAC Address"))
s:option(DummyValue, "hostname", translate("Hostname"))

status_col = s:option(DummyValue, "device_down", translate("Status"))
status_col.template = "acctl/ap_status"

s:option(DummyValue, "wan_ip",     translate("WAN IP"))
s:option(DummyValue, "wifi_ssid",  translate("SSID"))
s:option(DummyValue, "firmware",   translate("Firmware"))

users_col = s:option(DummyValue, "online_users", translate("Users"))
users_col.template = "acctl/ap_users"

s:option(DummyValue, "last_seen_str", translate("Last Seen"))

-- Refresh button
function s.render_footer(self)
	luci_http.write('<div class="cbi-page-actions">')
	luci_http.write('<input class="cbi-button cbi-button-action important" ' ..
		'type="submit" name="refresh" value="' .. translate("Refresh") .. '" />')
	luci_http.write('</div>')
end

-- Bulk actions
s2 = m:section(NamedSection, "acctl", "acctl",
	translate("Bulk Operations"))
s2.addremove = false

sel = s2:option(Value, "_selected", translate("Selected APs (comma-separated MACs)"))
sel.placeholder = "aa:bb:cc:dd:ee:ff, 11:22:33:44:55:66"
sel.datatype = "string"

action = s2:option(ListValue, "_action", translate("Action"))
action:value("none",     translate("Select action..."))
action:value("reboot",  translate("Reboot APs"))
action:value("upgrade",  translate("Upgrade Firmware"))
action:value("config",  translate("Push Configuration"))
action:value("offline",  translate("Mark Offline"))

apply_btn = s2:option(Button, "_apply", translate("Apply"))
apply_btn.inputstyle = "apply"

return m
