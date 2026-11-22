# Userspace Makefile for Loop-OS

ARCH           := x86_64
TARGET         := x86_64-elf
BUILD_DIR      := ../build/userspace
LIB_DIR        := ../build/userspace/lib

# Cross-compiler
CC             := $(TARGET)-gcc
CXX            := $(TARGET)-g++
AS             := $(TARGET)-as
LD             := $(TARGET)-ld
AR             := $(TARGET)-ar
OBJCOPY        := $(TARGET)-objcopy
STRIP          := $(TARGET)-strip

# Host tools
MKDIR          := mkdir -p
RM             := rm -rf
CP             := cp -r

# Flags
CFLAGS         := -std=gnu11 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
                  -mno-red-zone -O2 -g \
                  -Wall -Wextra -Wno-unused-parameter \
                  -I../userspace/libc/include \
                  -I../kernel/include \
                  -D_KERNEL_SYSCALLS

LDFLAGS        := -nostdlib -static -T ../userspace/user.ld

# Source directories
LIBC_SRCS      := $(wildcard ../userspace/libc/src/*.c)
INIT_SRCS      := $(wildcard ../userspace/init/*.c)

# Object files
LIBC_OBJS      := $(patsubst ../userspace/libc/src/%.c,$(BUILD_DIR)/libc/%.o,$(LIBC_SRCS))
INIT_OBJS      := $(patsubst ../userspace/init/%.c,$(BUILD_DIR)/init/%.o,$(INIT_SRCS))

# Output binaries
INIT_BIN       := $(BUILD_DIR)/init
SHELL_BIN      := $(BUILD_DIR)/sh
LIBC_A         := $(LIB_DIR)/libc.a

# Phony targets
.PHONY: all libc init shell clean install

all: libc init shell

# Build libc
libc: $(LIBC_A)

$(LIBC_A): $(LIBC_OBJS)
	@$(MKDIR) -p $(dir $@)
	$(AR) rcs $@ $^

# Build init
init: $(INIT_BIN)

$(INIT_BIN): $(BUILD_DIR)/init/init.o $(LIBC_A)
	@$(MKDIR) -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $< -L$(LIB_DIR) -lc

# Build shell
shell: $(SHELL_BIN)

$(SHELL_BIN): $(BUILD_DIR)/init/shell.o $(LIBC_A)
	@$(MKDIR) -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $< -L$(LIB_DIR) -lc

# Compile rules
$(BUILD_DIR)/libc/%.o: ../userspace/libc/src/%.c
	@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/init/%.o: ../userspace/init/%.c
	@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Install to ISO root
install: all
	@$(MKDIR) -p ../iso/overlay/bin ../iso/overlay/sbin ../iso/overlay/usr/bin
	$(CP) $(INIT_BIN) ../iso/overlay/sbin/init
	$(CP) $(SHELL_BIN) ../iso/overlay/bin/sh
	$(CP) $(SHELL_BIN) ../iso/overlay/bin/bash
	@echo "Userspace installed to ISO overlay"

clean:
	$(RM) $(BUILD_DIR) $(LIB_DIR)

# Help
help:
	@echo "Userspace Build Targets:"
	@echo "  make all       - Build libc, init, shell"
	@echo "  make libc      - Build libc.a only"
	@echo "  make init      - Build init binary"
	@echo "  make shell     - Build shell binary"
	@echo "  make install   - Install to ISO overlay"
	@echo "  make clean     - Remove build artifacts"