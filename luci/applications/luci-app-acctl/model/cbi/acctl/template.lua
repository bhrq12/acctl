--[[
AC Controller — Config Templates
]]--

m = Map("acctl", translate("Config Templates"),
	translate("AP default configuration templates"))

-- Default profiles
s = m:section(TypedSection, "node_default", translate("Profiles"),
	translate("Pre-configured AP profiles"))
s.anonymous = true
s.addremove = true

profile = s:option(Value, "profile", translate("Profile Name"))
profile.rmempty = false

devname = s:option(Value, "device_name", translate("Device Name"))
devname.placeholder = "AP-01"

ssid = s:option(Value, "wifi_ssid", translate("SSID"))
ssid.rmempty = false

enc = s:option(ListValue, "wifi_encryption", translate("Encryption"))
enc:value("psk2", "WPA2-PSK")
enc:value("psk", "WPA-PSK")
enc:value("wpa3", "WPA3-SAE")
enc:value("none", translate("Open (No Encryption)"))
enc.default = "psk2"
enc.rmempty = false

key = s:option(Value, "wifi_key", translate("WiFi Password"))
key.password = true
key.rmempty = false

mode = s:option(ListValue, "wifi_channel_mode", translate("Channel Mode"))
mode:value("auto", translate("Auto (Recommended)"))
mode:value("11g", "802.11g (2.4GHz)")
mode:value("11n", "802.11n (2.4GHz)")
mode:value("11a", "802.11a (5GHz)")
mode:value("11ac", "802.11ac (5GHz)")
mode.default = "auto"
mode.rmempty = false

channel = s:option(Value, "wifi_channel", translate("Channel"))
channel.placeholder = translate("Auto")

signal = s:option(Value, "wifi_signal", translate("Tx Power (dBm)"))
signal.placeholder = "auto"

return m
