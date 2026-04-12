include $(TOPDIR)/rules.mk

PKG_NAME:=acctl
PKG_VERSION:=1.1
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/acctl
  SECTION:=net
  CATEGORY:=Network
  TITLE:=OpenWrt AC Controller
  DEPENDS:=+libpthread +libsqlite3
  URL:=https://your-project-url
endef

define Package/acctl/description
  OpenWrt AC Controller for managing Access Points
  Uses SQLite database for lightweight operation
endef

define Build/Prepare
	$(CP) $(CURDIR)/../../../* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	# 直接编译lib目录
	cd $(PKG_BUILD_DIR)/lib && \
	$(MAKE) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include -I$(PKG_BUILD_DIR)/include" \
		LDFLAGS="$(TARGET_LDFLAGS) -L$(STAGING_DIR)/usr/lib"
	
	# 编译ac目录
	cd $(PKG_BUILD_DIR)/ac && \
	$(MAKE) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include -I$(PKG_BUILD_DIR)/include -DSERVER" \
		LDFLAGS="$(TARGET_LDFLAGS) -L$(STAGING_DIR)/usr/lib -lsqlite3" \
		LIBDIR="$(PKG_BUILD_DIR)/lib" \
		LIB="libapctl.o"
	
	# 编译ap目录
	cd $(PKG_BUILD_DIR)/ap && \
	$(MAKE) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include -I$(PKG_BUILD_DIR)/include -DCLIENT" \
		LDFLAGS="$(TARGET_LDFLAGS) -L$(STAGING_DIR)/usr/lib -lsqlite3" \
		LIBDIR="$(PKG_BUILD_DIR)/lib" \
		LIB="libapctl.o"
endef

define Package/acctl/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ac/acser $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ap/apctl $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) $(CURDIR)/files/acctl.init $(1)/etc/init.d/acctl
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) $(CURDIR)/files/acctl.config $(1)/etc/config/acctl
	$(INSTALL_DIR) $(1)/etc/acctl
endef

$(eval $(call BuildPackage,acctl))
