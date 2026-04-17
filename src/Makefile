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

.PHONY: all clean acser apctl acctl-cli libac.a

all: $(SERVER:%=acser%) $(CLIENT:%=apctl%) $(CLI:%=acctl-cli%)

# ============================================================================
# Shared library sources (common + AC-specific)
# ============================================================================
LIB_SRCS   = chap.c md5.c link.c thread.c sec.c sha256.c
LIB_SRCS  += dllayer.c netlayer.c arg.c cfgarg.c cmdarg.c mjson.c
LIB_OBJS   = $(LIB_SRCS:.c=.o)

# Generic .c -> .o (no SERVER/CLIENT flag)
$(LIB_SRCS:.c=.o): %.o: %.c
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

$(AC_OBJS): CFLAGS += -DSERVER
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

$(AP_OBJS): CFLAGS += -DCLIENT
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

$(CLI_OBJS): CFLAGS += -DCLI
$(CLI_OBJS): %.o: %.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

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
