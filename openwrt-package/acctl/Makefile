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
	# 创建构建目录
	mkdir -p $(PKG_BUILD_DIR)
	# 直接使用源代码目录，不复制文件
	ACCTL_SRC=$(CURDIR)/../../../../..
	ln -sf $(ACCTL_SRC)/ac $(PKG_BUILD_DIR)/ac
	ln -sf $(ACCTL_SRC)/ap $(PKG_BUILD_DIR)/ap
	ln -sf $(ACCTL_SRC)/include $(PKG_BUILD_DIR)/include
	ln -sf $(ACCTL_SRC)/lib $(PKG_BUILD_DIR)/lib
	ln -sf $(ACCTL_SRC)/scripts $(PKG_BUILD_DIR)/scripts
	ln -sf $(ACCTL_SRC)/Makefile $(PKG_BUILD_DIR)/Makefile
endef

define Build/Compile
	# 直接在源代码目录中编译
	ACCTL_SRC=$(CURDIR)/../../../../..
	
	# 编译lib目录
	cd $(ACCTL_SRC)/lib && \
	$(MAKE) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include -I$(ACCTL_SRC)/include" \
		LDFLAGS="$(TARGET_LDFLAGS) -L$(STAGING_DIR)/usr/lib"
	
	# 编译ac目录
	cd $(ACCTL_SRC)/ac && \
	$(MAKE) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include -I$(ACCTL_SRC)/include -DSERVER" \
		LDFLAGS="$(TARGET_LDFLAGS) -L$(STAGING_DIR)/usr/lib -lsqlite3" \
		LIBDIR="$(ACCTL_SRC)/lib" \
		LIB="libapctl.o"
	
	# 编译ap目录
	cd $(ACCTL_SRC)/ap && \
	$(MAKE) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include -I$(ACCTL_SRC)/include -DCLIENT" \
		LDFLAGS="$(TARGET_LDFLAGS) -L$(STAGING_DIR)/usr/lib -lsqlite3" \
		LIBDIR="$(ACCTL_SRC)/lib" \
		LIB="libapctl.o"
endef

define Package/acctl/install
	# 使用源代码目录中的编译产物
	ACCTL_SRC=$(CURDIR)/../../../../..
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(ACCTL_SRC)/ac/acser $(1)/usr/bin/
	$(INSTALL_BIN) $(ACCTL_SRC)/ap/apctl $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) $(CURDIR)/files/acctl.init $(1)/etc/init.d/acctl
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) $(CURDIR)/files/acctl.config $(1)/etc/config/acctl
	$(INSTALL_DIR) $(1)/etc/acctl
endef

$(eval $(call BuildPackage,acctl))
