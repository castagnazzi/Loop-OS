#!/bin/bash
# Build cross-compiler toolchain for Loop-OS (x86_64-elf with musl)
# Fixed: proper sysroot setup for musl headers before GCC stage 2

set -euo pipefail

# =============================================================================
# Configuration
# =============================================================================
TARGET=x86_64-elf
PREFIX="$(pwd)/../toolchain/local"
SYSROOT="$PREFIX/$TARGET"

# Versions (pinned for reproducibility)
BINUTILS_VER=2.43
GCC_VER=14.2.0
MUSL_VER=1.2.5
LINUX_HEADERS_VER=6.10

# Build settings
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
BUILD_DIR="$(pwd)/build-toolchain"

# =============================================================================
# Helpers
# =============================================================================
log() { echo -e "\033[1;32m[TOOLCHAIN]\033[0m $*"; }
err() { echo -e "\033[1;31m[ERROR]\033[0m $*" >&2; exit 1; }
download() {
    local url=$1 file=$2
    if [[ ! -f "$file" ]]; then
        log "Downloading $file..."
        curl -L -o "$file" "$url" || err "Failed to download $url"
    fi
}

# =============================================================================
# Main
# =============================================================================
mkdir -p "$BUILD_DIR" "$PREFIX"
cd "$BUILD_DIR"

log "Building toolchain for $TARGET"
log "Prefix: $PREFIX"
log "Sysroot: $SYSROOT"
log "Jobs: $JOBS"

# -----------------------------------------------------------------------------
# Download sources
# -----------------------------------------------------------------------------
download "https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VER}.tar.xz" "binutils-${BINUTILS_VER}.tar.xz"
download "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VER}/gcc-${GCC_VER}.tar.xz" "gcc-${GCC_VER}.tar.xz"
download "https://musl.libc.org/releases/musl-${MUSL_VER}.tar.gz" "musl-${MUSL_VER}.tar.gz"
download "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${LINUX_HEADERS_VER}.tar.xz" "linux-${LINUX_HEADERS_VER}.tar.xz"

# -----------------------------------------------------------------------------
# Extract
# -----------------------------------------------------------------------------
log "Extracting sources..."
for f in \
    "binutils-${BINUTILS_VER}.tar.xz" \
    "gcc-${GCC_VER}.tar.xz" \
    "musl-${MUSL_VER}.tar.gz" \
    "linux-${LINUX_HEADERS_VER}.tar.xz"; do
    log "Extracting $f..."
    tar -xf "$f"
done

# -----------------------------------------------------------------------------
# Install Linux headers (for musl)
# -----------------------------------------------------------------------------
log "Installing Linux headers..."
cd "linux-${LINUX_HEADERS_VER}"
make ARCH=x86_64 INSTALL_HDR_PATH="$SYSROOT/usr" headers_install
cd ..

# -----------------------------------------------------------------------------
# Build binutils
# -----------------------------------------------------------------------------
log "Building binutils..."
mkdir -p build-binutils
cd build-binutils
../binutils-${BINUTILS_VER}/configure \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --with-sysroot="$SYSROOT" \
    --disable-nls \
    --disable-werror \
    --enable-gold=yes \
    --enable-plugins \
    --enable-threads \
    --enable-deterministic-archives
make -j$JOBS
make install
cd ..

# -----------------------------------------------------------------------------
# Build GCC (stage 1 - without libc)
# -----------------------------------------------------------------------------
log "Building GCC stage 1..."
mkdir -p build-gcc-stage1
cd build-gcc-stage1
../gcc-${GCC_VER}/configure \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --with-sysroot="$SYSROOT" \
    --disable-nls \
    --enable-languages=c,c++ \
    --without-headers \
    --disable-shared \
    --disable-threads \
    --disable-libatomic \
    --disable-libgomp \
    --disable-libquadmath \
    --disable-libssp \
    --disable-libvtv \
    --disable-libstdcxx \
    --enable-default-pie \
    --enable-default-ssp
make -j$JOBS all-gcc
make install-gcc
cd ..

# -----------------------------------------------------------------------------
# Build musl libc - install to sysroot BEFORE GCC stage 2
# -----------------------------------------------------------------------------
log "Building musl libc..."
cd "musl-${MUSL_VER}"
./configure \
    --prefix=/usr \
    --disable-shared \
    CC="$PREFIX/bin/$TARGET-gcc" \
    AR="$PREFIX/bin/$TARGET-ar" \
    RANLIB="$PREFIX/bin/$TARGET-ranlib"
make -j$JOBS

# Install musl to sysroot
log "Installing musl to sysroot: $SYSROOT"
DESTDIR="$SYSROOT" make install

# CRITICAL: Copy musl headers to where GCC stage 2 expects them
# GCC stage 2 libgcc build looks in $SYSROOT/usr/include and $SYSROOT/usr/include/$TARGET
log "Ensuring musl headers are in sysroot for GCC stage 2..."
mkdir -p "$SYSROOT/usr/include/$TARGET"
# musl installs to /usr/include, copy to target-specific include dir too
cp -r "$SYSROOT/usr/include"/* "$SYSROOT/usr/include/$TARGET/" 2>/dev/null || true

# Verify critical headers exist
if [[ ! -f "$SYSROOT/usr/include/features.h" ]]; then
    err "FATAL: features.h not found at $SYSROOT/usr/include/features.h"
fi
if [[ ! -f "$SYSROOT/usr/include/$TARGET/features.h" ]]; then
    err "FATAL: features.h not found at $SYSROOT/usr/include/$TARGET/features.h"
fi
log "musl headers verified in sysroot"

# Also ensure linux headers are in place
cp -r "$SYSROOT/usr/include"/* "$SYSROOT/usr/include/$TARGET/" 2>/dev/null || true
cd ..

# -----------------------------------------------------------------------------
# Build GCC (stage 2 - with libc)
# -----------------------------------------------------------------------------
log "Building GCC stage 2..."
mkdir -p build-gcc-stage2
cd build-gcc-stage2
../gcc-${GCC_VER}/configure \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --with-sysroot="$SYSROOT" \
    --with-build-sysroot="$SYSROOT" \
    --disable-nls \
    --enable-languages=c,c++ \
    --disable-shared \
    --disable-threads \
    --disable-libatomic \
    --disable-libgomp \
    --disable-libquadmath \
    --disable-libssp \
    --disable-libvtv \
    --disable-libstdcxx
# Alvo bare-metal x86_64-elf: sem libc/crt para linkar executaveis, entao so
# gcc + libgcc (suficiente para o kernel freestanding).
make -j$JOBS all-gcc all-target-libgcc
make install-gcc install-target-libgcc
cd ..

# -----------------------------------------------------------------------------
# Verify
# -----------------------------------------------------------------------------
log "Verifying toolchain..."
"$PREFIX/bin/$TARGET-gcc" --version
"$PREFIX/bin/$TARGET-gcc" -print-sysroot

log "Toolchain built successfully!"
log "Add to PATH: export PATH=\"$PREFIX/bin:\$PATH\""
log "Or use: make toolchain (which sets up env)"