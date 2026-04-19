# acctl

[English](#english) | [中文](#中文)

---

# English

AC Controller for OpenWrt — manage enterprise WiFi Access Points centrally.

## Features

- **Centralized AC Server** (acser): manages APs via TCP control channel + Ethernet broadcast discovery
- **Lightweight AP Client** (apctl): runs on managed APs, auto-registers to AC, reports status periodically
- **CHAP Authentication**: shared secret stored in UCI config (`/etc/config/acctl`), no hardcoded passwords
- **JSON File Database**: zero-dependency JSON database (`/etc/acctl/ac.json`) using libjson-c; no SQLite
- **Atomic Database Persistence**: write-to-temp + rename pattern; 60s auto-save thread prevents data loss on crash
- **File-Locked CLI**: acctl-cli uses fcntl advisory locking to prevent race conditions with acser daemon
- **AP Grouping & Batch Config**: assign APs to groups, push SSID/channel/power settings in bulk
- **Alarm/Event System**: threshold-based alarms (AP offline, weak signal, CPU/memory overload, etc.)
- **Firmware OTA**: push firmware upgrades to managed APs over the control channel
- **Multi-SSID Management**: per-AP SSID templates with 2.4GHz/5GHz band separation
- **LuCI Web UI**: full management panel for AC config, AP list, groups, alarms, and firmware

## Architecture

```
+----------------+     TCP (port 7960)     +----------------+
|  AC Server    | <--------------------> |   AP Client    |
|   (acser)     |     ETH Broadcast       |   (apctl)     |
+----------------+  (discovery probe)      +----------------+
       |                                         |
   /etc/acctl/                              broadcasts
   ac.json (JSON DB)                       registration
       |
  LuCI Web UI
```

- **acser**: AC Controller daemon — runs on OpenWrt router acting as the WiFi controller
- **apctl**: AP Client daemon — runs on each managed Access Point hardware

## Build

Requires OpenWrt SDK or full buildroot with Lean/lede feeds.

```bash
# Copy package into OpenWrt build tree
cd /path/to/openwrt
cp -r acctl package/

# Install dependencies from feeds
./scripts/feeds install libuci-lua

# Compile
make package/acctl/compile V=99
```

## Install

The build produces `acctl_*.ipk`. Install on the AC router:

```bash
opkg install acctl_*.ipk
```

**Installation layout:**
- `/usr/bin/acser` — AC server binary
- `/usr/bin/apctl` — AP client binary
- `/usr/bin/acctl-cli` — CLI management tool
- `/etc/init.d/acctl` — AC server init script (procd)
- `/etc/init.d/apctl` — AP client init script (procd)
- `/etc/config/acctl` — UCI configuration
- `/etc/acctl/ac.json` — JSON database

## Configuration

Configure via UCI. All settings are in `/etc/config/acctl`:

```
# === AC Server ===
config acctl 'acctl'
    option enabled     '1'
    option interface   'br-lan'
    option port        '7960'

    # IMPORTANT: Set a strong password before starting!
    #   uci set acctl.@acctl[0].password='your_secure_password'
    #   uci commit acctl
    option password    ''

    option brditv      '30'       # broadcast probe interval (seconds)
    option reschkitv  '300'      # IP pool reload interval (seconds)
    option msgitv     '3'        # message processing interval (seconds)
    option daemon      '1'        # run as daemon
    option debug       '0'       # debug mode

# === AP Default Profiles ===
config profile 'profile_office'
    option name 'Office Network'
    list ssid_2g 'Office-2G'
    option enc_2g 'psk2'
    option key_2g 'changeme123'
    list ssid_5g 'Office-5G'
    option enc_5g 'psk2'
    option key_5g 'changeme123'

# === Alarm Rules ===
config alarm 'ap_offline'
    option name      'AP Offline'
    option level     '2'
    option threshold '1'
    option window    '60'
    option cooldown  '300'
    option enabled   '1'
```

Access via LuCI: **System → AC Controller**

## Project Structure

```
acctl/
|-- Makefile                 # OpenWrt package Makefile (-DSERVER flag)
|-- src/                     # C source (25 .c files, 21 .h files)
|   |-- include/             # Public headers: aphash, chap, db, dllayer, link,
|   |                       #   log, md5, msg, net, netlayer, sec, sha256, thread
|   |-- ac/                  # AC server (acser) source
|   |   ac.c, net.c, process.c, resource.c, message.c, db.c
|   |   aphash.c (shared AP hash table), cli.c (management CLI)
|   |-- ap/                  # AP client (apctl) source
|   |   main.c, net.c, process.c, message.c, apstatus.c
|   |-- lib/                 # Shared library: epoll, DLL, security, CHAP, JSON
|       arg.c, chap.c, cmdarg.c, dllayer.c, link.c, md5.c, mjson.c
|       netlayer.c, sec.c, sha256.c, thread.c
|-- luci/                    # LuCI web UI
|   |-- applications/luci-app-acctl/
|       controller/          # Lua controller (router registration)
|       model/cbi/acctl/     # CBI models (7 Lua config pages)
|       view/acctl/          # HTML view templates
|       root/etc/uci-defaults/
|-- files/                   # Files installed by ipk
    |-- etc/config/acctl     # UCI configuration template
    |-- etc/init.d/acctl     # AC server init (procd)
    |-- etc/init.d/apctl     # AP client init (procd)
```

## Dependencies

- `libuci-lua` — UCI configuration access
- `libjson-c` — JSON serialization (database)
- `libpthread` — POSIX threads
- `librt` — POSIX real-time extensions (shm_open, clock_gettime)
- `libubus` — OpenWrt system bus (optional)
- `libiwinfo` — wireless device info (optional)

## Supported Platforms

Tested on OpenWrt build targets:

| Target | Architecture | Devices |
|--------|-------------|---------|
| `ipq40xx` | ARM Cortex-A7 | QCN5502/CR660x/X1800/AX3600 |
| `x86_64` | x86_64 | x86 router boards |
| `bcm2711` | ARM Cortex-A72 | Raspberry Pi 4B |

## Security

- CHAP authentication: shared secret from UCI config, never stored in plaintext
- Dangerous-pattern-first command validation: shell metacharacters are rejected BEFORE whitelist matching, preventing injection through whitelisted prefixes
- Whitelisted commands with path exceptions (e.g. "cat /proc/uptime") use precise prefix matching with contains_path flag
- Command execution via sh -c with strict validation (recommended: migrate to execvp with argument array)
- Double validation removed from sec_exec_command (was redundant with caller)
- Per-AP rate limiting: 60 registrations/min, 120 commands/min
- Replay window protection
- HMAC-SHA256 message integrity
- AC trust list for AP authentication

### v2.0 Security Audit (2026-04-20)

Full source code audit completed — 35 issues found and fixed:

- **CRITICAL**: Command injection bypass via whitelist prefix (e.g. `wifi;rm -rf /`) — fixed: dangerous patterns checked first
- **HIGH**: Shell injection in LuCI controller (api_aps_action, api_cmd) — fixed: whitelist + regex sanitization
- **MEDIUM**: Data race between CLI tool and daemon (no file locking) — fixed: fcntl advisory locking
- **MEDIUM**: Global lock contention during message processing — fixed: snapshot + process outside lock
- **MEDIUM**: 5GHz band never detected (frequency range overlap) — fixed: check 5GHz before 2.4GHz
- **MEDIUM**: MAC address DB lookup always fails (raw bytes vs formatted string) — fixed

See `acctl-audit-report-20260420.md` for full details.

## License

MIT

---

# 中文

acctl — OpenWrt 企业级 AC 控制器，集中管理多个 AP（无线接入点）。

## 功能特性

- **集中式 AC 服务器**（acser）：通过 TCP 控制通道 + 以太网广播发现统一管理 AP
- **轻量级 AP 客户端**（apctl）：部署在各 AP 上，自动向 AC 注册并定时上报状态
- **CHAP 认证**：共享密钥存储在 UCI 配置文件（`/etc/config/acctl`）中，无硬编码密码
- **JSON 文件数据库**：基于 libjson-c 的零依赖 JSON 数据库（`/etc/acctl/ac.json`），无需 SQLite
- **原子数据库持久化**：写临时文件 + rename 模式；60 秒自动保存线程防止崩溃丢数据
- **文件锁 CLI**：acctl-cli 使用 fcntl 建议锁，避免与 acser 守护进程的竞争条件
- **AP 分组与批量配置**：将 AP 归入分组，批量下发 SSID / 信道 / 功率等配置
- **告警/事件系统**：基于阈值的告警（AP 离线、信号弱、CPU/内存过载等）
- **固件 OTA 推送**：通过控制通道向 AP 推送固件升级包
- **多 SSID 管理**：支持每个 AP 配置 2.4GHz / 5GHz 双频 SSID 模板
- **LuCI Web 管理界面**：完整的 Web 面板，支持 AC 配置、AP 列表、分组、告警、固件管理

## 架构

```
+----------------+     TCP (端口 7960)     +----------------+
|  AC 服务器     | <--------------------> |  AP 客户端     |
|   (acser)     |      以太网广播           |   (apctl)     |
+----------------+    (发现探测)             +----------------+
       |                                          |
   /etc/acctl/                               广播注册
   ac.json (JSON 数据库)                      请求
       |
  LuCI Web 管理界面
```

- **acser**：AC 控制器守护进程，运行在 OpenWrt 路由器上，作为 WiFi 控制器
- **apctl**：AP 客户端守护进程，运行在每个受管理的 AP 硬件上

## 编译

需要 OpenWrt SDK 或完整 buildroot（推荐 Lean/lede 源码）。

```bash
# 将 acctl 复制到 OpenWrt 编译树
cd /path/to/openwrt
cp -r acctl package/

# 安装 feeds 依赖
./scripts/feeds install libuci-lua

# 编译
make package/acctl/compile V=99
```

## 安装

编译产物为 `acctl_*.ipk`，安装到 AC 路由器：

```bash
opkg install acctl_*.ipk
```

**安装目录结构：**
- `/usr/bin/acser` — AC 服务器二进制文件
- `/usr/bin/apctl` — AP 客户端二进制文件
- `/usr/bin/acctl-cli` — 命令行管理工具
- `/etc/init.d/acctl` — AC 服务器启动脚本（procd）
- `/etc/init.d/apctl` — AP 客户端启动脚本（procd）
- `/etc/config/acctl` — UCI 配置文件
- `/etc/acctl/ac.json` — JSON 数据库文件

## 配置

所有配置通过 UCI 完成，编辑 `/etc/config/acctl`：

```
# === AC 服务器 ===
config acctl 'acctl'
    option enabled     '1'
    option interface   'br-lan'
    option port        '7960'

    # 重要：请在启动前设置密码！
    #   uci set acctl.@acctl[0].password='your_secure_password'
    #   uci commit acctl
    option password    ''

    option brditv      '30'       # 广播探测间隔（秒）
    option reschkitv  '300'      # IP 池重载间隔（秒）
    option msgitv     '3'        # 消息处理间隔（秒）
    option daemon      '1'        # 后台运行
    option debug       '0'        # 调试模式

# === AP 默认模板 ===
config profile 'profile_office'
    option name '办公网络'
    list ssid_2g 'Office-2G'
    option enc_2g 'psk2'
    option key_2g 'changeme123'
    list ssid_5g 'Office-5G'
    option enc_5g 'psk2'
    option key_5g 'changeme123'

# === 告警规则 ===
config alarm 'ap_offline'
    option name      'AP 离线'
    option level     '2'
    option threshold '1'
    option window    '60'
    option cooldown  '300'
    option enabled   '1'
```

通过 LuCI 访问：**系统 → AC 控制器**

## 项目结构

```
acctl/
|-- Makefile                 # OpenWrt 包 Makefile（-DSERVER 编译标志）
|-- src/                     # C 源码（25 个 .c 文件，21 个 .h 文件）
|   |-- include/             # 公共头文件：aphash, chap, db, dllayer, link,
|   |                       #   log, md5, msg, net, netlayer, sec, sha256, thread
|   |-- ac/                  # AC 服务器（acser）源码
|   |   ac.c, net.c, process.c, resource.c, message.c, db.c
|   |   aphash.c（共享 AP 哈希表）, cli.c（管理 CLI）
|   |-- ap/                  # AP 客户端（apctl）源码
|   |   main.c, net.c, process.c, message.c, apstatus.c
|   |-- lib/                 # 共享库：epoll, DLL, 安全, CHAP, JSON 等
|       arg.c, chap.c, cmdarg.c, dllayer.c, link.c, md5.c, mjson.c
|       netlayer.c, sec.c, sha256.c, thread.c
|-- luci/                    # LuCI Web 管理界面
|   |-- applications/luci-app-acctl/
|       controller/          # Lua 控制器（路由注册）
|       model/cbi/acctl/     # CBI 模型（7 个 Lua 配置页面）
|       view/acctl/          # HTML 视图模板
|       root/etc/uci-defaults/
|-- files/                   # ipk 安装文件
    |-- etc/config/acctl     # UCI 配置模板
    |-- etc/init.d/acctl    # AC 服务器启动脚本（procd）
    |-- etc/init.d/apctl    # AP 客户端启动脚本（procd）
```

## 依赖

- `libuci-lua` — UCI 配置访问
- `libjson-c` — JSON 序列化（数据库）
- `libpthread` — POSIX 线程
- `librt` — POSIX 实时扩展（shm_open, clock_gettime）
- `libubus` — OpenWrt 系统总线（可选）
- `libiwinfo` — 无线设备信息（可选）

## 支持的平台

已在以下 OpenWrt 编译目标上测试：

| 目标架构 | CPU 架构 | 代表设备 |
|---------|---------|---------|
| `ipq40xx` | ARM Cortex-A7 | QCN5502 / CR660x / X1800 / AX3600 |
| `x86_64` | x86_64 | x86 路由器 |
| `bcm2711` | ARM Cortex-A72 | Raspberry Pi 4B |

## 安全特性

- CHAP 认证：密钥仅存于 UCI 配置，纯文本不落地
- 危险模式优先的命令验证：先拒绝 Shell 元字符，再匹配白名单，防止通过白名单前缀注入
- 白名单含路径的命令（如 "cat /proc/uptime"）使用 contains_path 标记精确豁免
- 命令执行通过 sh -c 严格验证后执行（建议后续迁移到 execvp 参数数组模式）
- 单 AP 限流：每分钟最多 60 次注册、120 条命令
- 重放攻击窗口保护
- HMAC-SHA256 消息完整性校验
- AC 信任列表，AP 接入认证

### v2.0 安全审计（2026-04-20）

已完成全量源码审计，发现并修复 35 项问题：

- **严重**：通过白名单前缀绕过命令注入（如 `wifi;rm -rf /`）— 已修复：危险模式优先检查
- **高危**：LuCI 控制器 Shell 注入（api_aps_action、api_cmd）— 已修复：白名单 + 正则过滤
- **中危**：CLI 工具与守护进程数据竞争（无文件锁）— 已修复：fcntl 建议锁
- **中危**：消息处理期间全局锁竞争 — 已修复：快照 + 锁外处理
- **中危**：5GHz 频段永远检测不到（频率范围重叠）— 已修复：先检查 5GHz
- **中危**：MAC 地址数据库查询永远失败（原始字节 vs 格式化字符串）— 已修复

详见 `acctl-audit-report-20260420.md`。

## 许可证

MIT
