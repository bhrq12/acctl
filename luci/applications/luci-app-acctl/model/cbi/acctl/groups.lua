--[[
AC Controller — AP Groups
]]--

local sys = require "luci.sys"
local util = require "luci.util"

m = Map("acctl", translate("AP Groups"),
	translate("Organize Access Points into groups for batch management"))

function get_groups()
	local groups = {}
	local cmd = "sqlite3 /etc/acctl/ac.db " ..
		"'SELECT id,name,description,update_policy FROM ap_group' 2>/dev/null"
	local output = sys.exec(cmd)
	for row in output:gmatch("[^\r\n]+") do
		local fields = util.split(row, "|")
		if #fields >= 4 then
			local id = tonumber(fields[1]) or 0
			local ap_count = tonumber(sys.exec(string.format(
				"sqlite3 /etc/acctl/ac.db " ..
				"'SELECT COUNT(*) FROM node WHERE group_id=%d' 2>/dev/null", id))) or 0
			table.insert(groups, {
				id = id,
				name = fields[2] or "",
				description = fields[3] or "",
				policy = fields[4] or "manual",
				ap_count = ap_count
			})
		end
	end
	return groups
end

s = m:section(TypedSection, "ap_group", translate("Groups"),
	translate("Create and manage AP groups"))
s.anonymous = true
s.addremove = true

name = s:option(Value, "name", translate("Group Name"))
name.rmempty = false

desc = s:option(Value, "description", translate("Description"))
desc.placeholder = translate("Optional description")

policy = s:option(ListValue, "update_policy", translate("Update Policy"))
policy:value("manual", translate("Manual (no auto-upgrade)"))
policy:value("auto", translate("Auto (apply template on AP connect)"))
policy:value("rolling", translate("Rolling (upgrade one at a time)"))
policy.default = "manual"

-- Group member list
s2 = m:section(Table, get_groups(),
	translatef("Groups (%d)", #(get_groups())))

s2:option(DummyValue, "id", translate("ID"))
s2:option(DummyValue, "name", translate("Name"))
s2:option(DummyValue, "description", translate("Description"))
s2:option(DummyValue, "policy", translate("Policy"))
s2:option(DummyValue, "ap_count", translate("AP Count"))

return m
