--[[
AC Controller — Firmware Management
]]--

local sys = require "luci.sys"
local util = require "luci.util"
local fs = require "nixio.fs"

m = Map("acctl", translate("Firmware Management"),
	translate("Upload and manage AP firmware images"))

function get_firmwares()
	local fw_list = {}
	local cmd = "sqlite3 /etc/acctl/ac.db " ..
		"'SELECT version,filename,file_size,uploaded_at FROM firmware " ..
		"ORDER BY uploaded_at DESC' 2>/dev/null"
	local output = sys.exec(cmd)
	for row in output:gmatch("[^\r\n]+") do
		local fields = util.split(row, "|")
		if #fields >= 4 then
			table.insert(fw_list, {
				version = fields[1] or "",
				filename = fields[2] or "",
				size = tonumber(fields[3]) or 0,
				uploaded_at = fields[4] or ""
			})
		end
	end
	return fw_list
end

-- Upload section
s = m:section(NamedSection, "acctl", "acctl",
	translate("Upload New Firmware"))

upload_file = s:option(FileUpload, "_file", translate("Firmware File"))
upload_file.template = "acctl/upload"

version_field = s:option(Value, "_version", translate("Firmware Version"))
version_field.placeholder = "e.g. v2.0.1"

notes_field = s:option(TextValue, "_notes", translate("Release Notes"))
notes_field.rows = 3
notes_field.placeholder = translate("Optional release notes...")

upload_btn = s:option(Button, "_upload", translate("Upload"))
upload_btn.inputstyle = "apply"

-- Firmware list
s2 = m:section(Table, get_firmwares(),
	translatef("Uploaded Firmwares (%d)", #(get_firmwares())))

s2:option(DummyValue, "version", translate("Version"))
s2:option(DummyValue, "filename", translate("Filename"))

size_col = s2:option(DummyValue, "size", translate("Size (bytes)"))
s2:option(DummyValue, "uploaded_at", translate("Uploaded At"))

-- Upgrade section
s3 = m:section(NamedSection, "acctl", "acctl",
	translate("Batch Upgrade"))

target_version = s3:option(ListValue, "_target_version",
	translate("Target Version"))
for _, fw in ipairs(get_firmwares()) do
	target_version:value(fw.version, fw.version)
end

upgrade_target = s3:option(Value, "_target_macs",
	translate("Target APs (comma-separated MACs, or 'all')"))
upgrade_target.placeholder = "all"

upgrade_policy = s3:option(ListValue, "_upgrade_policy",
	translate("Upgrade Strategy"))
upgrade_policy:value("immediate", translate("Immediate (all at once)"))
upgrade_policy:value("rolling", translate("Rolling (one by one)"))
upgrade_policy:value("scheduled", translate("Scheduled (at night)"))

upgrade_btn = s3:option(Button, "_start_upgrade",
	translate("Start Upgrade"))
upgrade_btn.inputstyle = "apply"

return m
