# OpenWrt AC Controller v2.0

A production-grade Access Point centralized management system for OpenWrt.
Build, manage, and monitor hundreds of APs from a single AC controller.

## Overview

```
┌─────────────────────────────────────────────────────────┐
│                  AC Controller (acser)                    │
│  TCP:7960 LISTEN    SQLite DB    LuCI Web UI           │
│  ETH Broadcast       AP Hash     REST API              │
└──────────────────────────┬──────────────────────────────┘
                           │
        ┌──────────────────┼──────────────────┐
        │                  │                  │
   ┌────▼────┐        ┌────▼────┐        ┌────▼────┐
   │ AP-1F-01│        │ AP-2F-03│        │ AP-3F-02│
   │ (apctl) │        │ (apctl) │        │ (apctl) │
   └─────────┘        └─────────┘        └─────────┘
```

## Features

### Core
- **TCP + ETH Broadcast** dual-layer communication
- **CHAP + MD5** authentication (password from UCI config, no hardcoding)
- **SQLite** lightweight database (AP inventory, status, config)
- **IP Address Pool** management with conflict detection
- **AP Groups** for batch management

### Security (v2.0)
- **Command whitelist** — blocks 30+ dangerous patterns (pipes, redirects, shell escapes)
- **Rate limiting** — per-AP registration and command rate controls
- **Replay protection** — sliding-window random-number tracking
- **Takeover prevention** — AC trust whitelist
- **HMAC integrity** — message authenticity verification

### Management
- **LuCI Web UI** — complete management interface with AJAX polling
- **REST API** — JSON endpoints for all operations
- **Alarm System** — configurable threshold-based alerts
- **Firmware OTA** — upgrade management infrastructure
- **Audit Log** — all operations logged to database

## Quick Start

### 1. Configure Password (Required!)

```bash
# Set authentication password
uci set acctl.@acctl[0].password='your_secure_password'
uci commit acctl

# Or create secret file
echo 'your_secure_password' > /etc/acctl/password
chmod 600 /etc/acctl/password
```

### 2. Start AC Controller

```bash
# Via procd (production)
/etc/init.d/acctl enable
/etc/init.d/acctl start

# Manually (with debug)
acser -n br-lan -d -l 4
```

### 3. Configure IP Pool

```bash
sqlite3 /etc/acctl/ac.db <<'EOF'
INSERT OR REPLACE INTO resource VALUES ('192.168.100.1', '192.168.100.200', '255.255.255.0');
EOF
```

### 4. Start AP Client

```bash
# On each managed AP
/etc/init.d/apctl enable
/etc/init.d/apctl start

# Or manually
apctl -n eth0 -a 192.168.1.1 -r 30 -d
```

## Directory Structure

```
acctl/
├── Makefile                 # OpenWrt package Makefile
├── README.md
│
├── src/
│   ├── ac/                  # AC Controller (server)
│   │   ├── main.c          # Entry point, daemon init
│   │   ├── process.c        # Message handling, AP registration
│   │   ├── net.c           # Network layer (TCP + ETH)
│   │   ├── message.c       # Per-AP message queues
│   │   ├── sql.c           # SQLite operations
│   │   ├── resource.c      # IP pool management
│   │   └── aphash.c       # AP hash table
│   │
│   ├── ap/                 # AP Client (managed device)
│   │   ├── main.c          # Entry point
│   │   ├── process.c       # AC discovery, registration
│   │   ├── message.c       # Message queue
│   │   ├── apstatus.c      # System status collection
│   │   └── net.c          # Datalink receive
│   │
│   ├── lib/                # Shared library
│   │   ├── chap.c          # CHAP authentication
│   │   ├── sec.c           # Security layer (command whitelist, rate limit)
│   │   ├── dllayer.c       # Ethernet datalink
│   │   ├── netlayer.c      # TCP networking
│   │   ├── link.c          # epoll multiplexing
│   │   ├── md5.c           # MD5 algorithm
│   │   ├── mjson.c         # JSON parser
│   │   ├── thread.c        # pthread wrapper
│   │   ├── arg.c           # UCI + command-line parsing
│   │   ├── cfgarg.c        # UCI config reader
│   │   └── cmdarg.c        # getopt_long parser
│   │
│   └── include/            # Header files
│       ├── msg.h           # Protocol messages
│       ├── chap.h          # CHAP declarations
│       ├── sec.h           # Security API
│       ├── aphash.h        # AP hash table
│       ├── process.h       # Process declarations
│       ├── resource.h       # IP pool API
│       ├── sql.h           # Database API
│       ├── apstatus.h      # AP status structs
│       ├── message.h       # Message queue API
│       ├── link.h          # epoll API
│       ├── dllayer.h       # Datalink API
│       ├── netlayer.h      # TCP API
│       ├── net.h           # Network abstraction
│       ├── arg.h           # Arguments API
│       ├── log.h           # Logging macros
│       ├── thread.h         # Thread API
│       ├── md5.h           # MD5 API
│       ├── mjson.h         # JSON API
│       └── list.h           # Linux kernel list macros
│
├── luci/                   # LuCI Web UI
│   └── applications/
│       └── luci-app-acctl/
│           ├── controller/acctl.lua      # Router + API
│           ├── model/cbi/
│           │   ├── general.lua          # General settings
│           │   ├── ap_list.lua          # AP list management
│           │   ├── ap_group.lua         # Group management
│           │   ├── alarm.lua            # Alarm center
│           │   ├── template.lua         # Config templates
│           │   ├── firmware.lua         # Firmware mgmt
│           │   └── system.lua           # System info
│           └── view/acctl/              # Templates
│               ├── status_bar.htm
│               ├── ap_status_badge.htm
│               ├── alarm_level_badge.htm
│               └── user_badge.htm
│
└── files/
    ├── etc/
    │   ├── config/acctl   # UCI configuration
    │   └── init.d/
    │       ├── acctl       # AC server init script
    │       └── apctl       # AP client init script
    └── acctl/
        └── (database files)
```

## Command Line Usage

### AC Server (acser)

```bash
acser -n <interface> [-d] [-p <port>] [-b <brditv>] [-c <reschkitv>] [-m <msgitv>] [-l <level>]

  -n, --nic       Network interface (required)     e.g. -n br-lan
  -d, --daemon    Run as daemon
  -p, --port      TCP listen port (default: 7960)
  -b, --brditv    Broadcast interval seconds (default: 30)
  -c, --reschkitv IP pool reload interval (default: 300)
  -m, --msgitv    Message processing interval (default: 3)
  -l, --debug     Debug level: 1=debug, 2=warn, 3=lock, 4=all
  -h, --help      Show help
```

### AP Client (apctl)

```bash
apctl -n <interface> [-d] [-p <port>] [-a <ac_domain>] [-r <reportitv>] [-m <msgitv>]

  -n, --nic        Network interface (required)
  -d, --daemon     Run as daemon
  -p, --port       AC listen port (default: 7960)
  -a, --domain      Remote AC domain/IP (required for remote AC)
  -r, --reportitv  Status report interval seconds (default: 30)
  -m, --msgitv     Message processing interval (default: 3)
  -h, --help       Show help
```

## REST API

All endpoints return JSON. Base URL: `/cgi-bin/luci/admin/network/acctl/api`

### Status
```
GET /status → {"running":true,"ap_online":48,"ap_total":52,"alarm_count":2}
```

### AP List
```
GET /aps → {"count":52,"aps":[{"mac":"AA:BB:CC:DD:EE:FF","status":"online",...},...]}
GET /aps?mac=AA:BB:CC:DD:EE:FF → single AP detail
```

### Alarms
```
GET /alarms?limit=50 → {"alarms":[{"id":1,"mac":"AA:BB:CC:DD:EE:FF","level":"warn",...},...]}
```

### Commands
```
POST /cmd
Body: mac=AA:BB:CC:DD:EE:FF&cmd=uptime
```

## Building

### OpenWrt SDK

```bash
# Copy package to OpenWrt
cp -r acctl /path/to/openwrt/package/

# Build
make package/acctl/compile V=s

# Or with the SDK
cd /path/to/openwrt/sdk
./scripts/feeds update acctl
./scripts/feeds install acctl
make package/acctl/compile V=s
```

### Standalone (Linux)

```bash
cd src/lib
make
# Outputs: acser (AC server), apctl (AP client)
```

## Database Schema

| Table | Description |
|-------|-------------|
| `resource` | IP address pool (start/end/mask) |
| `node` | AP device info (MAC, hostname, status, config) |
| `node_default` | AP config templates (profiles) |
| `node_setting` | MAC-specific pre-configuration |
| `ap_group` | AP group definitions |
| `alarm_event` | Alarm history |
| `firmware` | Firmware repository |
| `upgrade_log` | Upgrade tracking |
| `audit_log` | Operation audit trail |

## Security Notes

1. **Set a strong password** — default is empty, no AP can register without it
2. **Use HTTPS for LuCI** — API endpoints transmit sensitive data
3. **Firewall AC port** — only allow AP subnet to reach TCP:7960
4. **Command whitelist is strict** — only predefined commands are allowed on APs
5. **Rate limits enforced** — prevents registration floods and DoS

## OpenWrt Compatibility

Tested on:
- OpenWrt 18.06.x (LEDE 18.06)
- OpenWrt 21.02.x
- OpenWrt 22.03.x
- OpenWrt 23.05.x / SNAPSHOT

Required packages:
- `libsqlite3`
- `libpthread`
- `libuci-lua` (for LuCI)

## License

Original: Copyright (c) 2014 jianxi sun <ycsunjane@gmail.com>
v2.0 Improvements: 2026 — enhanced for production use

---

For support, issues, or contributions, please open a GitHub issue.
