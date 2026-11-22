# Loop-OS Build Environment
# Reproducible Docker image for building the OS

FROM debian:bookworm-slim AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc \
    g++ \
    make \
    cmake \
    ninja-build \
    bison \
    flex \
    libgmp-dev \
    libmpfr-dev \
    libmpc-dev \
    libisl-dev \
    texinfo \
    xorriso \
    qemu-system-x86 \
    qemu-utils \
    grub-pc-bin \
    grub-efi-amd64-bin \
    mtools \
    dosfstools \
    curl \
    wget \
    git \
    python3 \
    pkg-config \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
ARG UID=1000
ARG GID=1000
RUN groupadd -g $GID builder && useradd -m -u $UID -g $GID builder

USER builder
WORKDIR /home/builder

# Build toolchain as builder user
COPY --chown=builder:builder build-toolchain.sh /home/builder/build-toolchain.sh
RUN chmod +x /home/builder/build-toolchain.sh && /home/builder/build-toolchain.sh

# Add toolchain to PATH
ENV PATH="/home/builder/toolchain/local/bin:${PATH}"

# Default command
CMD ["/bin/bash"]