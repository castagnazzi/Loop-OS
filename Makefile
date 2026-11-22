# Loop-OS Main Makefile
# Build system for kernel, userspace, and ISO

# =============================================================================
# Configuration
# =============================================================================
ARCH           := x86_64
TARGET         := x86_64-elf
BUILD_DIR      := build
KERNEL_DIR     := kernel
USERSPACE_DIR  := userspace
ISO_DIR        := iso
TOOLCHAIN_DIR  := toolchain

# Cross-compiler (set by toolchain/build-toolchain.sh)
CC             := $(TARGET)-gcc
CXX            := $(TARGET)-g++
AS             := $(TARGET)-as
LD             := $(TARGET)-ld
AR             := $(TARGET)-ar
OBJCOPY        := $(TARGET)-objcopy
STRIP          := $(TARGET)-strip
NM             := $(TARGET)-nm

# Host tools
HOST_CC        := gcc
HOST_CXX       := g++
MKDIR          := mkdir -p
RM             := rm -rf
CP             := cp -r

# =============================================================================
# Compiler Flags
# =============================================================================
# Kernel flags
KERNEL_CFLAGS  := -std=gnu11 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
                  -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mno-sse3 -mno-3dnow \
                  -mcmodel=kernel -O2 -g \
                  -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
                  -I$(KERNEL_DIR)/include -I$(KERNEL_DIR)/arch/$(ARCH)/include

KERNEL_ASFLAGS := -felf64
KERNEL_LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T $(KERNEL_DIR)/kernel.ld

# Userspace flags
USER_CFLAGS    := -std=gnu11 -fPIC -O2 -g \
                  -Wall -Wextra -Wno-unused-parameter \
                  -I$(USERSPACE_DIR)/libc/include \
                  -I$(USERSPACE_DIR)/libgui/include \
                  -I$(USERSPACE_DIR)/wm/include \
                  -I$(USERSPACE_DIR)/desktop/include

USER_LDFLAGS   := -nostdlib -shared -fPIC

# =============================================================================
# Output Paths
# =============================================================================
KERNEL_BIN     := $(BUILD_DIR)/kernel.elf
KERNEL_BIN_STRIP := $(BUILD_DIR)/kernel.strip.elf
ISO_FILE       := $(BUILD_DIR)/loop-os.iso

# =============================================================================
# Source Discovery
# =============================================================================
KERNEL_C_SRCS  := $(shell find $(KERNEL_DIR) -name '*.c')
KERNEL_ASM_SRCS := $(shell find $(KERNEL_DIR) -name '*.S')
KERNEL_OBJS    := $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/kernel/%.o,$(KERNEL_C_SRCS)) \
                  $(patsubst $(KERNEL_DIR)/%.S,$(BUILD_DIR)/kernel/%.o,$(KERNEL_ASM_SRCS))

# =============================================================================
# Phony Targets
# =============================================================================
.PHONY: all kernel userspace iso run debug clean toolchain docker-build help

# =============================================================================
# Default Target
# =============================================================================
all: iso

# =============================================================================
# Toolchain
# =============================================================================
toolchain:
	@echo "=== Building Cross-Compiler Toolchain ==="
	@cd $(TOOLCHAIN_DIR) && ./build-toolchain.sh

# =============================================================================
# Kernel Build
# =============================================================================
kernel: $(KERNEL_BIN)

$(KERNEL_BIN): $(KERNEL_OBJS) $(KERNEL_DIR)/kernel.ld
	@echo "=== Linking Kernel ==="
	@$(MKDIR) -p $(dir $@)
	$(LD) $(KERNEL_LDFLAGS) $(KERNEL_OBJS) -o $@
	@echo "Kernel: $@"
	@$(OBJCOPY) --strip-debug $@ $(KERNEL_BIN_STRIP) 2>/dev/null || true

$(BUILD_DIR)/kernel/%.o: $(KERNEL_DIR)/%.c
	@$(MKDIR) -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/%.o: $(KERNEL_DIR)/%.S
	@$(MKDIR) -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

# =============================================================================
# Userspace Build (placeholder - Fase 3+)
# =============================================================================
userspace:
	@echo "=== Building Userspace ==="
	@$(MAKE) -C $(USERSPACE_DIR)

# =============================================================================
# ISO Build
# =============================================================================
iso: kernel $(ISO_FILE)

# Initial ramdisk: everything under rootfs/ plus the built userspace programs,
# packed as a ustar archive that Limine loads as a module.
ROOTFS_SRC     := rootfs
ROOTFS_DIR     := $(BUILD_DIR)/rootfs
INITRD         := $(BUILD_DIR)/initrd.tar

.PHONY: initrd
initrd:
	@echo "=== Building initrd ==="
	@$(RM) $(ROOTFS_DIR)
	@$(MKDIR) $(ROOTFS_DIR)
	@$(CP) $(ROOTFS_SRC)/. $(ROOTFS_DIR)/
	@$(MAKE) -C $(USERSPACE_DIR) install ROOTFS=$(abspath $(ROOTFS_DIR))
	@tar --format=ustar -cf $(INITRD) -C $(ROOTFS_DIR) .
	@echo "Initrd: $(INITRD)"

$(ISO_FILE): kernel initrd
	@echo "=== Building ISO ==="
	@cd $(ISO_DIR) && ./build-iso.sh $(abspath $(BUILD_DIR)) $(abspath $(KERNEL_BIN)) $(abspath $(INITRD))

# =============================================================================
# QEMU Targets
# =============================================================================
# Run with KVM if available
run: iso
	@echo "=== Running in QEMU ==="
	@./scripts/qemu-run.sh $(ISO_FILE)

# Debug with GDB (port 1234)
debug: iso
	@echo "=== Running in QEMU (GDB on port 1234) ==="
	@./scripts/qemu-debug.sh $(ISO_FILE)

# =============================================================================
# Docker Build (reproducible)
# =============================================================================
docker-build:
	@echo "=== Building in Docker ==="
	@docker build -t loop-os-builder -f $(TOOLCHAIN_DIR)/Dockerfile .
	@docker run --rm -v $(PWD):/src loop-os-builder make

# =============================================================================
# Clean
# =============================================================================
clean:
	@echo "=== Cleaning ==="
	$(RM) $(BUILD_DIR)

distclean: clean
	@$(MAKE) -C $(TOOLCHAIN_DIR) clean 2>/dev/null || true

# =============================================================================
# Help
# =============================================================================
help:
	@echo "Loop-OS Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make toolchain    - Build cross-compiler (run once)"
	@echo "  make kernel       - Build kernel only"
	@echo "  make userspace    - Build userspace (Fase 3+)"
	@echo "  make iso          - Build bootable ISO"
	@echo "  make run          - Run ISO in QEMU"
	@echo "  make debug        - Run with GDB stub on port 1234"
	@echo "  make docker-build - Build in Docker container"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make distclean    - Clean everything including toolchain"
	@echo "  make help         - This help"