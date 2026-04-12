-- AC Controller SQLite Database Initialization Script
-- Version 1.0

-- 表的结构 `resource`
CREATE TABLE IF NOT EXISTS resource (
  ip_start TEXT,
  ip_end TEXT,
  ip_mask TEXT
);

-- 表的结构 `node`
CREATE TABLE IF NOT EXISTS node (
  hostname TEXT,
  time_first TEXT,
  time TEXT,
  latitude TEXT,
  longitude TEXT,
  uptime TEXT,
  memfree TEXT,
  cpu TEXT,
  device_down INTEGER DEFAULT 0,
  wan_iface TEXT,
  wan_ip TEXT,
  wan_mac TEXT UNIQUE,
  wan_gateway TEXT,
  wifi_iface TEXT,
  wifi_ip TEXT,
  wifi_mac TEXT,
  wifi_ssid TEXT,
  wifi_encryption TEXT,
  wifi_key TEXT,
  wifi_channel_mode TEXT,
  wifi_channel TEXT,
  wifi_signal TEXT,
  lan_iface TEXT,
  lan_mac TEXT,
  lan_ip TEXT,
  wan_bup TEXT,
  wan_bup_sum TEXT,
  wan_bdown TEXT,
  wan_bdown_sum TEXT,
  firmware TEXT,
  firmware_revision TEXT,
  online_user_num INTEGER DEFAULT 0
);

-- 表的结构 `node_default`
CREATE TABLE IF NOT EXISTS node_default (
  profile TEXT PRIMARY KEY,
  device_name TEXT,
  wifi_ssid TEXT NOT NULL,
  wifi_encryption TEXT NOT NULL,
  wifi_key TEXT NOT NULL,
  wifi_channel_mode TEXT NOT NULL,
  wifi_channel TEXT,
  wifi_signal TEXT
);

-- 表的结构 `node_setting`
CREATE TABLE IF NOT EXISTS node_setting (
  pre_device_name TEXT,
  pre_device_mac TEXT UNIQUE,
  pre_device_description TEXT,
  device_latitude TEXT,
  device_longitude TEXT,
  wan_ip TEXT,
  wan_mac TEXT UNIQUE,
  wifi_ip TEXT,
  wifi_ssid TEXT,
  wifi_encryption TEXT,
  wifi_key TEXT,
  wifi_channel_mode TEXT,
  wifi_channel TEXT,
  wifi_signal TEXT
);
