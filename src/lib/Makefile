# Makefile for acctl shared library
#
# Builds: libapctl.a (static library with all modules)
# Also builds: acser (AC server) and apctl (AP client)
#
# Usage:
#   make              — build everything
#   make clean        — clean build artifacts
#   make ac_lib      — build AC server library
#   make ap_lib      — build AP client library
#   make acser       — build AC server binary
#   make apctl       — build AP client binary
#

CC       ?= gcc
CFLAGS   ?= -Wall -Wextra -O2 -g
LDFLAGS  ?=

# Directories
SRC_DIR   = .
INC_DIR   = ../include
OUT_DIR   = .

# Source files (shared)
LIB_SRCS  = chap.c md5.c link.c thread.c sec.c
LIB_OBJS  = $(LIB_SRCS:.c=.o)

# AC-specific sources
AC_SRCS   = ../ac/main.c ../ac/process.c ../ac/net.c ../ac/message.c \
            ../ac/resource.c ../ac/sql.c ../ac/aphash.c
AC_OBJS   = $(AC_SRCS:.c=.o)

# AP-specific sources
AP_SRCS   = ../ap/main.c ../ap/process.c ../ap/message.c ../ap/net.c \
            ../ap/apstatus.c
AP_OBJS   = $(AP_SRCS:.c=.o)

# Config
ifdef SERVER
  # Building AC server
  CFLAGS += -DSERVER -I$(INC_DIR)
  LIB_OBJS += dllayer.o netlayer.o arg.o cfgarg.o cmdarg.o mjson.o
  TARGET   = acser
  TARGET_OBJS = $(AC_OBJS) $(LIB_OBJS)
else ifdef CLIENT
  # Building AP client
  CFLAGS += -DCLIENT -I$(INC_DIR)
  LIB_OBJS += dllayer.o netlayer.o arg.o cfgarg.o cmdarg.o mjson.o
  TARGET   = apctl
  TARGET_OBJS = $(AP_OBJS) $(LIB_OBJS)
else
  # Default: build both via sub-makes
  TARGET   =
endif

.PHONY: all clean ac_lib ap_lib acser apctl

all: ac_lib ap_lib
	@echo "Build complete."
	@echo "  acser (AC server)  -> src/ac/"
	@echo "  apctl (AP client)  -> src/ap/"

ac_lib:
	@$(MAKE) SERVER=1 CC="$(CC)" CFLAGS="$(CFLAGS) -DSERVER" \
		acser

ap_lib:
	@$(MAKE) CLIENT=1 CC="$(CC)" CFLAGS="$(CFLAGS) -DCLIENT" \
		apctl

acser: dllayer.o netlayer.o arg.o cfgarg.o cmdarg.o mjson.o \
        chap.o md5.o link.o thread.o sec.o \
        ../ac/main.o ../ac/process.o ../ac/net.o ../ac/message.o \
        ../ac/resource.o ../ac/sql.o ../ac/aphash.o
	@echo "  Linking acser..."
	$(CC) $(CFLAGS) -o $@ $^ -lsqlite3 -lpthread -lm
	@echo "  acser built successfully."

apctl: dllayer.o netlayer.o arg.o cfgarg.o cmdarg.o mjson.o \
        chap.o md5.o link.o thread.o sec.o \
        ../ap/main.o ../ap/process.o ../ap/message.o ../ap/net.o \
        ../ap/apstatus.o
	@echo "  Linking apctl..."
	$(CC) $(CFLAGS) -o $@ $^ -lpthread -lm
	@echo "  apctl built successfully."

# Generic rules
%.o: %.c
	@echo "  CC $<"
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

../ac/%.o: ../ac/%.c
	@echo "  CC $<"
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

../ap/%.o: ../ap/%.c
	@echo "  CC $<"
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

clean:
	rm -f *.o ../ac/*.o ../ap/*.o acser apctl
	@echo "Clean complete."

# Dependencies
chap.o: chap.c $(INC_DIR)/chap.h $(INC_DIR)/msg.h $(INC_DIR)/log.h
md5.o: md5.c $(INC_DIR)/md5.h
link.o: link.c $(INC_DIR)/link.h $(INC_DIR)/log.h
thread.o: thread.c $(INC_DIR)/thread.h $(INC_DIR)/log.h
sec.o: sec.c $(INC_DIR)/sec.h $(INC_DIR)/log.h $(INC_DIR)/chap.h
dllayer.o: dllayer.c $(INC_DIR)/dllayer.h $(INC_DIR)/log.h
netlayer.o: netlayer.c $(INC_DIR)/netlayer.h $(INC_DIR)/log.h
arg.o: arg.c $(INC_DIR)/arg.h $(INC_DIR)/log.h
cfgarg.o: cfgarg.c
cmdarg.o: cmdarg.c $(INC_DIR)/arg.h $(INC_DIR)/log.h
mjson.o: mjson.c $(INC_DIR)/mjson.h

../ac/main.o: ../ac/main.c $(INC_DIR)/arg.h $(INC_DIR)/net.h \
	$(INC_DIR)/msg.h $(INC_DIR)/aphash.h $(INC_DIR)/process.h \
	$(INC_DIR)/netlayer.h $(INC_DIR)/resource.h $(INC_DIR)/sql.h \
	$(INC_DIR)/sec.h $(INC_DIR)/log.h
../ac/process.o: ../ac/process.c $(INC_DIR)/net.h $(INC_DIR)/log.h \
	$(INC_DIR)/msg.h $(INC_DIR)/aphash.h $(INC_DIR)/arg.h \
	$(INC_DIR)/netlayer.h $(INC_DIR)/apstatus.h $(INC_DIR)/link.h \
	$(INC_DIR)/process.h $(INC_DIR)/resource.h $(INC_DIR)/sec.h
../ac/net.o: ../ac/net.c $(INC_DIR)/dllayer.h $(INC_DIR)/aphash.h \
	$(INC_DIR)/net.h $(INC_DIR)/log.h $(INC_DIR)/msg.h \
	$(INC_DIR)/arg.h $(INC_DIR)/thread.h $(INC_DIR)/process.h \
	$(INC_DIR)/link.h $(INC_DIR)/chap.h
../ac/message.o: ../ac/message.c $(INC_DIR)/log.h $(INC_DIR)/arg.h \
	$(INC_DIR)/msg.h $(INC_DIR)/thread.h $(INC_DIR)/aphash.h \
	$(INC_DIR)/process.h
../ac/resource.o: ../ac/resource.c $(INC_DIR)/log.h $(INC_DIR)/resource.h \
	$(INC_DIR)/sql.h $(INC_DIR)/mjson.h
../ac/sql.o: ../ac/sql.c $(INC_DIR)/sql.h $(INC_DIR)/log.h $(INC_DIR)/mjson.h
../ac/aphash.o: ../ac/aphash.c $(INC_DIR)/aphash.h $(INC_DIR)/log.h

../ap/main.o: ../ap/main.c $(INC_DIR)/arg.h $(INC_DIR)/message.h \
	$(INC_DIR)/process.h $(INC_DIR)/msg.h $(INC_DIR)/net.h \
	$(INC_DIR)/netlayer.h $(INC_DIR)/log.h $(INC_DIR)/link.h \
	$(INC_DIR)/thread.h $(INC_DIR)/sec.h
../ap/process.o: ../ap/process.c $(INC_DIR)/msg.h $(INC_DIR)/log.h \
	$(INC_DIR)/dllayer.h $(INC_DIR)/netlayer.h $(INC_DIR)/link.h \
	$(INC_DIR)/net.h $(INC_DIR)/arg.h $(INC_DIR)/thread.h \
	$(INC_DIR)/process.h $(INC_DIR)/apstatus.h $(INC_DIR)/sec.h
../ap/message.o: ../ap/message.c $(INC_DIR)/log.h $(INC_DIR)/arg.h \
	$(INC_DIR)/msg.h $(INC_DIR)/thread.h $(INC_DIR)/process.h
../ap/net.o: ../ap/net.c $(INC_DIR)/dllayer.h $(INC_DIR)/log.h \
	$(INC_DIR)/msg.h $(INC_DIR)/arg.h $(INC_DIR)/thread.h \
	$(INC_DIR)/process.h $(INC_DIR)/link.h
../ap/apstatus.o: ../ap/apstatus.c $(INC_DIR)/apstatus.h $(INC_DIR)/log.h
