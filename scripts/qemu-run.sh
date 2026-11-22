#!/bin/bash
# Run Loop-OS in QEMU

set -euo pipefail

ISO_FILE="${1:-build/loop-os.iso}"
KVM_ARGS=""

# Check for KVM
if [[ -c /dev/kvm ]] && groups | grep -q kvm; then
    KVM_ARGS="-enable-kvm -cpu host"
    echo "KVM enabled"
else
    echo "KVM not available, using TCG"
fi

# QEMU command
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
    "$@"