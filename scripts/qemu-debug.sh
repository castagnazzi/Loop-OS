#!/bin/bash
# Run Loop-OS in QEMU with GDB stub

set -euo pipefail

ISO_FILE="${1:-build/loop-os.iso}"
GDB_PORT="${2:-1234}"

KVM_ARGS=""
if [[ -c /dev/kvm ]] && groups | grep -q kvm; then
    KVM_ARGS="-enable-kvm -cpu host"
fi

echo "Starting QEMU with GDB on port $GDB_PORT"
echo "Connect with: gdb -ex 'target remote localhost:$GDB_PORT' build/kernel.elf"

qemu-system-x86_64 \
    $KVM_ARGS \
    -m 512M \
    -smp 2 \
    -machine q35,accel=kvm:tcg \
    -bios /usr/share/ovmf/OVMF.fd \
    -drive file="$ISO_FILE",format=raw,if=virtio \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0 \
    -serial stdio \
    -display gtk,gl=on \
    -device virtio-gpu-pci \
    -s -S \
    "$@"