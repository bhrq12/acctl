--[[
AC Controller — AP Groups (JSON backend)
]]

local sys  = require "luci.sys"
local http = require "luci.http"

m = Map("acctl", translate("AP Groups"),
	translate("Organize Access Points into groups for batch management"))

-- Read groups from JSON file
local function get_groups()
	local groups = {}
	local f = io.open("/etc/acctl/ac.json", "r")
	if not f then return groups end

	local ok, data = pcall(http.parse_json, f:read("*a"))
	f:close()
	if not ok or not data or not data.ap_groups then return groups end

	for _, grp in ipairs(data.ap_groups) do
		-- Count APs in this group
		local ap_count = 0
		if data.nodes and type(data.nodes) == "table" then
			for _, node in ipairs(data.nodes) do
				if tonumber(node.group_id) == tonumber(grp.id) then
					ap_count = ap_count + 1
				end
			end
		end

		table.insert(groups, {
			id          = tonumber(grp.id) or 0,
			name        = grp.name or "",
			description = grp.description or "",
			policy      = grp.update_policy or "manual",
			ap_count    = ap_count
		})
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

-- Group list display
local grp_list = get_groups()
s2 = m:section(Table, grp_list,
	translatef("Groups (%d)", #grp_list))

s2:option(DummyValue, "id", translate("ID"))
s2:option(DummyValue, "name", translate("Name"))
s2:option(DummyValue, "description", translate("Description"))
s2:option(DummyValue, "policy", translate("Policy"))
s2:option(DummyValue, "ap_count", translate("AP Count"))

return m
