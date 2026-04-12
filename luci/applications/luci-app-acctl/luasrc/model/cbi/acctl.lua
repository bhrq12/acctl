m = Map('acctl', translate('AC Controller Configuration'), translate('Configure AC Controller settings'))

-- General Settings
s = m:section(TypedSection, 'general', translate('General Settings'))
s.anonymous = true

enabled = s:option(Flag, 'enabled', translate('Enable AC Controller'))
enabled.rmempty = false

tcp_port = s:option(Value, 'tcp_port', translate('TCP Port'))
tcp_port.datatype = 'port'
tcp_port.default = '12345'
tcp_port.rmempty = false

-- AP Settings
ap = m:section(TypedSection, 'ap', translate('Access Point Settings'))
ap.anonymous = true

ap_name = ap:option(Value, 'name', translate('AP Name'))
ap_name.rmempty = false

ap_ip = ap:option(Value, 'ip', translate('AP IP Address'))
ap_ip.datatype = 'ipaddr'
ap_ip.rmempty = false

ap_mac = ap:option(Value, 'mac', translate('AP MAC Address'))
ap_mac.datatype = 'macaddr'
ap_mac.rmempty = false

ap_secret = ap:option(Value, 'secret', translate('AP Secret'))
ap_secret.password = true
ap_secret.rmempty = false

return m
