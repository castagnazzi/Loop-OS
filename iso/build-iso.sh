#!/bin/bash
# Build Loop-OS ISO (Live + Install)
# Usage: ./build-iso.sh <build_dir> <kernel_elf>

set -euo pipefail

BUILD_DIR="${1:-../build}"
KERNEL_ELF="${2:-../build/kernel.elf}"
INITRD="${3:-}"            # optional tar archive loaded as a Limine module
ISO_NAME="loop-os.iso"
ISO_PATH="$BUILD_DIR/$ISO_NAME"

# Limine (binary release branch, supports base revision 3)
LIMINE_BRANCH="v8.x-binary"
LIMINE_REPO="https://github.com/limine-bootloader/limine.git"

WORK_DIR="$(mktemp -d)"
trap "rm -rf '$WORK_DIR'" EXIT

echo "=== Building Loop-OS ISO ==="
echo "Build dir: $BUILD_DIR"
echo "Kernel: $KERNEL_ELF"
echo "Output: $ISO_PATH"

# -----------------------------------------------------------------------------
# Download Limine if needed
# -----------------------------------------------------------------------------
# Cached outside WORK_DIR so it is only cloned/built once
LIMINE_DIR="$BUILD_DIR/limine-${LIMINE_BRANCH}"
if [[ ! -f "$LIMINE_DIR/limine" ]]; then
    echo "Fetching Limine (${LIMINE_BRANCH})..."
    rm -rf "$LIMINE_DIR"
    mkdir -p "$BUILD_DIR"
    git clone --depth=1 --branch "$LIMINE_BRANCH" "$LIMINE_REPO" "$LIMINE_DIR"
    make -C "$LIMINE_DIR"
fi

# -----------------------------------------------------------------------------
# Prepare ISO root
# -----------------------------------------------------------------------------
ISO_ROOT="$WORK_DIR/iso_root"
mkdir -p "$ISO_ROOT/boot/limine"
mkdir -p "$ISO_ROOT/EFI/BOOT"

# Copy kernel
cp "$KERNEL_ELF" "$ISO_ROOT/boot/kernel.elf"

# Copy limine config
cat > "$ISO_ROOT/boot/limine/limine.conf" << 'EOF'
# Loop-OS Limine Configuration (Limine 8.x syntax)
timeout: 5

/Loop-OS
    protocol: limine
    path: boot():/boot/kernel.elf
    cmdline: loglevel=debug

/Loop-OS (Verbose)
    protocol: limine
    path: boot():/boot/kernel.elf
    cmdline: loglevel=debug vga=verbose

/Loop-OS (Safe Mode)
    protocol: limine
    path: boot():/boot/kernel.elf
    cmdline: loglevel=debug nomodeset noacpi
EOF

# Initial ramdisk, handed to the kernel as a Limine module on every entry
if [[ -n "$INITRD" && -f "$INITRD" ]]; then
    echo "Initrd: $INITRD"
    cp "$INITRD" "$ISO_ROOT/boot/initrd.tar"
    sed -i 's|^\(    cmdline:.*\)$|\1\n    module_path: boot():/boot/initrd.tar|' \
        "$ISO_ROOT/boot/limine/limine.conf"
fi

# Copy Limine bootloader files
cp "$LIMINE_DIR/limine-bios.sys" "$ISO_ROOT/boot/limine/"
cp "$LIMINE_DIR/limine-bios-cd.bin" "$ISO_ROOT/boot/limine/"
cp "$LIMINE_DIR/limine-uefi-cd.bin" "$ISO_ROOT/boot/limine/"
cp "$LIMINE_DIR/BOOTX64.EFI" "$ISO_ROOT/EFI/BOOT/"
cp "$LIMINE_DIR/BOOTIA32.EFI" "$ISO_ROOT/EFI/BOOT/"

# -----------------------------------------------------------------------------
# Create ISO with xorriso
# -----------------------------------------------------------------------------
echo "Creating ISO..."
xorriso -as mkisofs \
    -R -r -J -joliet-long \
    -b boot/limine/limine-bios-cd.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label \
    -o "$ISO_PATH" \
    "$ISO_ROOT"

# Make ISO bootable on USB (hybrid)
"$LIMINE_DIR/limine" bios-install "$ISO_PATH"

echo "=== ISO created: $ISO_PATH ==="
ls -lh "$ISO_PATH"