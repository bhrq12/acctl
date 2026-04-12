# OpenWrt AC控制器使用说明

## 项目简介

OpenWrt AC控制器(acctl)是一个用于管理和控制AP(Access Point)设备的网络控制器系统，包含服务端(AC)和客户端(AP)两大核心组件。使用轻量级的SQLite数据库，非常适合在OpenWrt设备上运行。

## 功能特性

- 同一网段，二层发现，自动配置IP
- 自动二层/三层发送控制信息
- 二层主动探测网络中AP，可接管其他AC控制AP
- 客户端主动链接，并保持长链接
- 基于CHAP的安全认证机制，防止重放攻击
- 使用轻量级SQLite数据库，无需单独的数据库服务器

## OpenWrt编译指南

### 准备工作

1. **安装OpenWrt SDK**
   - 下载适合目标设备的OpenWrt SDK
   - 解压到本地目录

2. **安装依赖**
   ```bash
   ./scripts/feeds update -a
   ./scripts/feeds install libpthread libsqlite3
   ```

### 编译步骤

1. **复制包目录**
   - 将 `openwrt-package/acctl` 目录复制到 OpenWrt SDK 的 `package` 目录

2. **配置编译选项**
   ```bash
   make menuconfig
   ```
   - 在 `Network` 菜单中选择 `acctl`

3. **编译**
   ```bash
   make package/acctl/compile V=99
   ```

4. **安装**
   - 编译完成后，在 `bin/packages/` 目录找到生成的 `.ipk` 文件
   - 使用 `opkg` 安装到 OpenWrt 设备
   ```bash
   opkg install acctl_1.0-1_*.ipk
   ```

## 配置与使用

### 数据库

系统使用SQLite数据库，数据库文件位于 `/etc/acctl/ac.db`，会在首次启动时自动创建。

### 配置文件

配置文件位于 `/etc/config/acctl`，主要配置项：

```config
config acctl
    option interface 'eth0'  # 网络接口
    option port '8888'       # AC监听端口
    option password 'your_password'  # CHAP密码
```

### 启动服务

```bash
# 启动AC服务
/etc/init.d/acctl start

# 设置开机自启
/etc/init.d/acctl enable
```

### 运行AP客户端

在AP设备上运行：

```bash
apctl -i eth0
```

## 网络拓扑

```
AC (OpenWrt设备) <---> AP1 (OpenWrt设备)
               <---> AP2 (OpenWrt设备)
               <---> AP3 (OpenWrt设备)
```

## 故障排查

### 常见问题

1. **AP无法发现AC**
   - 检查网络连接是否正常
   - 确保AC和AP使用相同的CHAP密码
   - 检查网络接口配置是否正确

2. **AC无法接收AP状态**
   - 检查TCP连接是否正常
   - 查看系统日志：`logread | grep acctl`

3. **数据库问题**
   - 确保数据库文件有正确的权限
   - 检查磁盘空间是否充足

## 注意事项

1. **资源限制**：OpenWrt设备通常资源有限，建议在管理AP数量较多时选择性能较好的设备作为AC

2. **网络安全**：确保CHAP密码足够复杂，定期更换

3. **版本兼容性**：本项目适用于最新版本的OpenWrt系统

4. **依赖项**：确保安装了所有必要的依赖包

5. **SQLite优势**：相比MySQL，SQLite更轻量，无需单独服务器，非常适合嵌入式设备

## 版本历史

- v1.1: 使用SQLite替代MySQL，更适合OpenWrt环境
- v1.0: 初始版本，支持基本的AP管理功能
