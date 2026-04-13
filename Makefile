include $(TOPDIR)/rules.mk

PKG_NAME:=acctl
PKG_VERSION:=2.0
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(KERNEL_BUILD_DIR)/$(PKG_NAME)

PKG_CONFIG_DEPENDS:=

# Build dependencies use SOURCE package names (not binary package names).
# sqlite3 is the OpenWrt feed source package; it produces libsqlite3.
# libpthread is built into musl on modern OpenWrt — not a separate package.
PKG_BUILD_DEPENDS:=sqlite3
PKG_INSTALL_DEPENDS:=

include $(INCLUDE_DIR)/package.mk

define Package/acctl
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Access Points/Controllers
  TITLE:=OpenWrt AC Controller v2.0
  DEPENDS:=+libsqlite3 +libuci-lua
  URL:=https://github.com/yourname/acctl
  MAINTAINER:=jianxi sun <ycsunjane@gmail.com>
endef

define Package/acctl/description
  OpenWrt AC Controller v2.0

  Features:
  - AC server (acser) manages APs centrally via TCP + ETH broadcast
  - AP client (apctl) runs on each managed Access Point
  - CHAP authentication with UCI-stored passwords (no hardcoded secrets)
  - SQLite database for AP configuration and status
  - AP grouping and batch configuration
  - Alarm/event logging
  - Firmware OTA upgrade support
  - LuCI web management interface

  Requires: OpenWrt 18.06+
endef

define Package/acctl/conffiles
/etc/config/acctl
endef

# BuildPrepare copies source into staging directory with proper layout:
#   src/    -> $(PKG_BUILD_DIR)/src/
#   luci/   -> $(PKG_BUILD_DIR)/luci/
#   files/  -> $(PKG_BUILD_DIR)/files/
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src   $(PKG_BUILD_DIR)/
	$(CP) ./luci  $(PKG_BUILD_DIR)/
	$(CP) ./files $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	# Build shared library + both binaries via the lib Makefile
	$(MAKE) -C $(PKG_BUILD_DIR)/src/lib \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -I$(PKG_BUILD_DIR)/src/include \
			-I$(STAGING_DIR)/usr/include \
			-DDEBUG -Wall -Wextra" \
		LDFLAGS="$(TARGET_LDFLAGS) -L$(STAGING_DIR)/usr/lib \
			-lsqlite3 -lpthread -lm" \
		all
endef

define Package/acctl/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/src/lib/acser $(1)/usr/bin/acser
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/src/lib/apctl $(1)/usr/bin/apctl

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/files/etc/init.d/acctl $(1)/etc/init.d/acctl
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/files/etc/init.d/apctl $(1)/etc/init.d/apctl
	chmod 755 $(1)/etc/init.d/acctl
	chmod 755 $(1)/etc/init.d/apctl

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/files/etc/config/acctl $(1)/etc/config/acctl

	$(INSTALL_DIR) $(1)/etc/acctl
	touch $(1)/etc/acctl/ac.db

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
endef

$(eval $(call BuildPackage,acctl))
