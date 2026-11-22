#!/bin/bash
# Loop-OS Build Script for WSL2/Ubuntu

set -euo pipefail

# Use current directory (where script is run from)
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# ISO destination on C: drive (only C: available)
ISO_DEST="/mnt/c/loop-os.iso"

echo "=== Loop-OS Build in WSL2 ==="
echo "Project: $PROJECT_DIR"
echo "ISO output: $ISO_DEST"

cd "$PROJECT_DIR"

# 1. Install dependencies
echo "--- Installing build dependencies ---"
sudo apt update
sudo apt install -y --no-install-recommends \
    build-essential gcc g++ make cmake ninja-build \
    bison flex libgmp-dev libmpfr-dev libmpc-dev libisl-dev \
    texinfo xorriso qemu-system-x86 grub-pc-bin grub-efi-amd64-bin \
    mtools dosfstools curl wget git

# 2. Build toolchain (only if not exists)
if [[ ! -f "$HOME/loop-os/toolchain/local/bin/x86_64-elf-gcc" ]]; then
    echo "--- Building cross-compiler toolchain (~15-20 min) ---"
    make toolchain
else
    echo "--- Toolchain already exists, skipping ---"
fi

# 3. Add toolchain to PATH
export PATH="$HOME/loop-os/toolchain/local/bin:$PATH"

# 4. Build kernel + ISO
echo "--- Building kernel + ISO ---"
make kernel
make iso

# 5. Copy ISO to Windows
echo "--- Copying ISO to Windows ---"
cp build/loop-os.iso "$ISO_DEST"

echo ""
echo "=== BUILD COMPLETO ==="
echo "ISO criada: $ISO_DEST"
echo ""
echo "No Windows PowerShell, rode:"
echo "  winget install QEMU.QEMU"
echo "  qemu-system-x86_64.exe -m 512M -drive file=C:\loop-os.iso,format=raw,if=virtio -serial stdio -display gtk"