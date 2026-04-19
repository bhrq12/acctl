# Makefile for acctl source build
# Supports OpenWrt SDK cross-compilation and standalone builds
#
# Usage (OpenWrt SDK):
#   make -C src CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" \
#     CFLAGS="$(TARGET_CFLAGS) -I$(PKG_BUILD_DIR)/src/include" \
#     LDFLAGS="$(TARGET_LDFLAGS) -lpthread -lm -ljson-c" \
#     all
#
# Usage (standalone):
#   make -C src

TOPDIR ?= .
INC_DIR := $(TOPDIR)/include
OUT_DIR := .

CC       ?= gcc
CXX      ?= g++
AR       ?= ar
INSTALL  ?= install
RM       ?= rm -f

# Build flags
CFLAGS   ?= -Wall -Wextra -O2 -g -I$(INC_DIR)
LDFLAGS  ?= -lpthread -lm

# Targets
SERVER ?= 1
CLIENT ?= 1
CLI    ?= 1

# Default: Enable SERVER for OpenWrt builds (required for ac_t definitions)
ifeq ($(SERVER),1)
  CFLAGS += -DSERVER
endif

.PHONY: all clean acser apctl acctl-cli libac.a

all: $(if $(SERVER),acser) $(if $(CLIENT),apctl) $(if $(CLI),acctl-cli)

# ============================================================================
# Shared library sources (common + AC-specific) - located in lib/
# ============================================================================
LIB_SRCS   = lib/chap.c lib/md5.c lib/link.c lib/thread.c lib/sec.c lib/sha256.c
LIB_SRCS  += lib/dllayer.c lib/netlayer.c lib/arg.c lib/cfgarg.c lib/cmdarg.c lib/mjson.c
LIB_OBJS   = $(LIB_SRCS:.c=.o)

# Generic .c -> .o (no SERVER/CLIENT flag)
$(LIB_OBJS): lib/%.o: lib/%.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

# ============================================================================
# AC server (acser)
# ============================================================================
AC_SRCS   = ac/main.c ac/process.c ac/net.c ac/message.c \
            ac/resource.c ac/db.c ac/aphash.c ac/cli.c
AC_OBJS   = $(AC_SRCS:.c=.o)

acser: CFLAGS += -DSERVER
acser: LDFLAGS += -ljson-c
acser: $(LIB_OBJS) $(AC_OBJS) libac.a
	$(CC) $(filter-out -l%,$(LDFLAGS)) $^ -o $@ $(filter -l%,$(LDFLAGS))

$(AC_OBJS): %.o: %.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

# ============================================================================
# AP client (apctl)
# ============================================================================
AP_SRCS   = ap/main.c ap/process.c ap/message.c ap/net.c ap/apstatus.c
AP_OBJS   = $(AP_SRCS:.c=.o)

apctl: CFLAGS += -DCLIENT
apctl: $(LIB_OBJS) $(AP_OBJS) libac.a
	$(CC) $(filter-out -l%,$(LDFLAGS)) $^ -o $@ $(filter -l%,$(LDFLAGS))

$(AP_OBJS): %.o: %.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

# ============================================================================
# AC CLI tool (acctl-cli)
# ============================================================================
CLI_SRCS   = ac/cli.c ac/db.c
CLI_OBJS   = $(CLI_SRCS:.c=.o)

acctl-cli: CFLAGS += -DCLI
acctl-cli: LDFLAGS += -ljson-c
acctl-cli: $(LIB_OBJS) $(CLI_OBJS) libac.a
	$(CC) $(filter-out -l%,$(LDFLAGS)) $^ -o $@ $(filter -l%,$(LDFLAGS))

# ============================================================================
# Static library
# ============================================================================
libac.a: $(LIB_OBJS)
	$(AR) rcs $@ $^

# ============================================================================
# Clean
# ============================================================================
clean:
	$(RM) *.o ac/*.o ap/*.o acser apctl acctl-cli libac.a