include $(TOPDIR)/rules.mk

PKG_NAME:=acctl
PKG_VERSION:=2.0
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/acctl
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Access Controllers
  TITLE:=AC Controller - AP Management System
  DEPENDS:=+libuci-lua +libjson-c +libpthread +libubus +libiwinfo
  URL:=https://github.com/bhrq12/acctl
  MAINTAINER:=jianxi sun <ycsunjane@gmail.com>
endef

define Package/acctl/description
  OpenWrt AC Controller v2.0

  Features:
  - AC server (acser) manages APs centrally via TCP + ETH broadcast
  - AP client (apctl) runs on each managed Access Point
  - CHAP authentication with UCI-stored passwords (no hardcoded secrets)
  - JSON file-based database for AP configuration and status
  - AP grouping and batch configuration
  - Alarm/event logging
  - Firmware OTA upgrade support
  - LuCI web management interface

  Requires: OpenWrt 18.06+
endef

define Package/acctl/conffiles
/etc/config/acctl
endef

define Build/Prepare
	$(CP) ./src   $(PKG_BUILD_DIR)/src
	$(CP) ./luci  $(PKG_BUILD_DIR)/luci
	$(CP) ./files $(PKG_BUILD_DIR)/files
endef

define Build/Configure
endef

define Build/Compile
	+$(MAKE) -C $(PKG_BUILD_DIR)/src \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -I$(PKG_BUILD_DIR)/src/include -Wall -Wextra" \
		LDFLAGS="$(TARGET_LDFLAGS) -lpthread -lm -ljson-c" \
		all
endef

define Package/acctl/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/src/acser $(1)/usr/bin/acser
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/src/apctl $(1)/usr/bin/apctl
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/src/acctl-cli $(1)/usr/bin/acctl-cli

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/files/etc/init.d/acctl $(1)/etc/init.d/acctl
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/files/etc/init.d/apctl $(1)/etc/init.d/apctl

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/files/etc/config/acctl $(1)/etc/config/acctl

	$(INSTALL_DIR) $(1)/etc/acctl
	touch $(1)/etc/acctl/ac.json

	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/controller
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/luci/applications/luci-app-acctl/luasrc/controller/acctl.lua \
		$(1)/usr/lib/lua/luci/controller/acctl.lua

	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/model/cbi/acctl
	for f in $(PKG_BUILD_DIR)/luci/applications/luci-app-acctl/model/cbi/acctl/*.lua; do \
		$(INSTALL_DATA) "$$f" $(1)/usr/lib/lua/luci/model/cbi/acctl/; \
	done

	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/view/acctl
	for f in $(PKG_BUILD_DIR)/luci/applications/luci-app-acctl/view/acctl/*.htm; do \
		$(INSTALL_DATA) "$$f" $(1)/usr/lib/lua/luci/view/acctl/; \
	done

	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/luci/applications/luci-app-acctl/root/etc/uci-defaults/luci-app-acctl \
		$(1)/etc/uci-defaults/luci-app-acctl
endef

$(eval $(call BuildPackage,acctl))
