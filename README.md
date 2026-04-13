# acctl

OpenWrt AC 控制器。集中管理多台 AP 的配置、状态和固件升级。

## 功能特性

- **集中管控**：AC 服务器（acser）通过 TCP + ETH 广播同时管理多台 AP
- **零配置接入**：AP 上电后自动注册到 AC，无需手动配置
- **分组管理**：支持 AP 分组，批量下发配置和固件
- **固件 OTA**：支持 AP 固件远程升级
- **事件告警**：实时上报 AP 上下线、告警等信息
- **LuCI 管理界面**：Web UI 配置 AC 和查看 AP 状态

## 系统架构

```
┌─────────────┐      TCP + ETH广播      ┌──────────────┐
│  AC 服务器   │ ◄─────────────────────► │   AP 客户端   │
│  (acser)    │      管理+心跳+固件      │   (apctl)    │
└─────────────┘                          └──────────────┘
     │                                           ▲
     ▼                                           │
  SQLite ──────── AP配置/状态/固件 ──────────────┘
```

- **acser**：运行在 OpenWrt AC 路由器，监听 AP 注册、接收告警、批量管理
- **apctl**：运行在每台 AP，监听 AC 下发的配置指令，周期上报状态

## 编译

需要 OpenWrt SDK 或完整源码编译环境（Lean 大源码 / OpenWrt 官方）。

```bash
# 方式一：放入 OpenWrt 源码 package 目录后编译
cd $OPENWRT_DIR
cp -r acctl package/
make package/acctl/compile V=s

# 方式二：使用 SDK
# 适用于 x86_64、ipq40xx、bcm2711 等主流平台
./scripts/feeds install libuci-lua
make package/acctl/compile V=s
```

## 安装

编译产物为 `acctl_*.ipk`，在 AC 路由器上安装：

```bash
opkg install acctl_*.ipk
```

安装后：
- 二进制文件：`/usr/bin/acser`、`/usr/bin/apctl`
- 启动脚本：`/etc/init.d/acctl`、`/etc/init.d/apctl`
- UCI 配置：`/etc/config/acctl`
- 数据库：`/etc/acctl/ac.db`

## 配置

所有配置通过 UCI 管理，示例 `/etc/config/acctl`：

```
config acctl 'general'
    option enabled '1'
    option log_level 'info'

config acctl 'ac'
    option port '9999'
    list broadcast_if 'br-lan'

config acctl 'ap'
    option heartbeat_interval '30'
    option ota_check '1'
```

Web 管理界面：`LuCI → 网络 → AC 控制器`

## 目录结构

```
acctl/
├── Makefile                 # OpenWrt 软件包 Makefile
├── src/                     # C 源码
│   ├── ac/                  # AC 服务器 (acser) 源码
│   ├── ap/                  # AP 客户端 (apctl) 源码
│   ├── lib/                 # 共享库 + Makefile
│   ├── include/             # 头文件
│   └── scripts/             # SQL 建表脚本
├── luci/                    # LuCI Web 管理界面
└── files/                   # 打包进 ipk 的文件
    └── etc/init.d/          # OpenWrt 启动脚本
```

## 依赖

- `libsqlite3`
- `libuci-lua`
- `libpthread`（musl 内置）

## 平台支持

已在以下平台验证编译通过：

| 平台 | 架构 | 设备示例 |
|------|------|----------|
| `ipq40xx` | ARM Cortex-A7 | 小米 CR660x、AX1800/AX3600 |
| `x86_64` | x86_64 | 软路由 |
| `bcm2711` | ARM Cortex-A72 | 树莓派 4B |

## License

MIT
