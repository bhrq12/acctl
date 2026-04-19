# Makefile for acctl source build
# Supports OpenWrt SDK cross-compilation and standalone builds
#
# Usage (OpenWrt SDK):
#   make -C src CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" \
#     CFLAGS="$(TARGET_CFLAGS) -I$(PKG_BUILD_DIR)/src/include -DSERVER" \
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

.PHONY: all clean acser apctl acctl-cli

all: $(if $(SERVER),acser) $(if $(CLIENT),apctl) $(if $(CLI),acctl-cli)

# ============================================================================
# Shared library sources - located in lib/
# AC and AP need different compile flags, so we build separate object dirs.
# ============================================================================
LIB_SRCS   = lib/chap.c lib/md5.c lib/link.c lib/thread.c lib/sec.c lib/sha256.c
LIB_SRCS  += lib/dllayer.c lib/netlayer.c lib/arg.c lib/cfgarg.c lib/cmdarg.c lib/mjson.c

# AC-specific lib objects (with -DSERVER)
AC_LIB_OBJS = $(patsubst lib/%.c,ac_lib/%.o,$(LIB_SRCS))

# AP-specific lib objects (with -DCLIENT)
AP_LIB_OBJS = $(patsubst lib/%.c,ap_lib/%.o,$(LIB_SRCS))

# CLI-specific lib objects (no SERVER/CLIENT)
CLI_LIB_OBJS = $(patsubst lib/%.c,cli_lib/%.o,$(LIB_SRCS))

# Create output directories
$(shell mkdir -p ac_lib ap_lib cli_lib)

# AC lib objects
ac_lib/%.o: lib/%.c
	$(CC) $(CFLAGS) -DSERVER -I$(INC_DIR) -c $< -o $@

# AP lib objects
ap_lib/%.o: lib/%.c
	$(CC) $(CFLAGS) -DCLIENT -I$(INC_DIR) -c $< -o $@

# CLI lib objects (no SERVER/CLIENT flags)
cli_lib/%.o: lib/%.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

# ============================================================================
# AC server (acser) — compiled with -DSERVER
# ============================================================================
AC_SRCS   = ac/main.c ac/process.c ac/net.c ac/message.c \
            ac/resource.c ac/db.c ac/aphash.c ac/cli.c
AC_OBJS   = $(AC_SRCS:.c=.o)

acser: $(AC_LIB_OBJS) $(AC_OBJS)
	$(CC) $(LDFLAGS) $^ -ljson-c -o $@

$(AC_OBJS): %.o: %.c
	$(CC) $(CFLAGS) -DSERVER -I$(INC_DIR) -c $< -o $@

# ============================================================================
# AP client (apctl) — compiled with -DCLIENT
# ============================================================================
AP_SRCS   = ap/main.c ap/process.c ap/message.c ap/net.c ap/apstatus.c
AP_OBJS   = $(AP_SRCS:.c=.o)

apctl: $(AP_LIB_OBJS) $(AP_OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

$(AP_OBJS): %.o: %.c
	$(CC) $(CFLAGS) -DCLIENT -I$(INC_DIR) -c $< -o $@

# ============================================================================
# AC CLI tool (acctl-cli) — no SERVER/CLIENT
# ============================================================================
CLI_SRCS   = ac/cli.c ac/db.c
CLI_OBJS   = $(CLI_SRCS:.c=.o)

acctl-cli: $(CLI_LIB_OBJS) $(CLI_OBJS)
	$(CC) $(LDFLAGS) $^ -ljson-c -o $@

$(CLI_OBJS): %.o: %.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

# ============================================================================
# Clean
# ============================================================================
clean:
	$(RM) -r ac_lib ap_lib cli_lib
	$(RM) ac/*.o ap/*.o acser apctl acctl-cli