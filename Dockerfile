# Multi-stage Dockerfile for ascii-chat production builds
# Supports multi-arch: linux/amd64, linux/arm64

# ============================================================================
# Stage 1: Builder
# ============================================================================
FROM ubuntu:24.04 AS builder

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install git and basic utilities first
RUN apt-get update && apt-get install -y \
  git \
  curl \
  ca-certificates \
  gpg \
  wget \
  lsb-release \
  && rm -rf /var/lib/apt/lists/*

# Add Kitware APT repository for latest CMake
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null \
  | gpg --dearmor - \
  | tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null \
  && echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ noble main' \
  | tee /etc/apt/sources.list.d/kitware.list >/dev/null

# Install build dependencies
RUN apt-get update && apt-get install -y \
  pkg-config make autoconf automake libtool \
  cmake ninja-build ccache \
  musl-tools musl-dev \
  libmimalloc-dev libzstd-dev zlib1g-dev libsodium-dev portaudio19-dev libopus-dev \
  libffi-dev \
  libprotobuf-c-dev libprotobuf-c1 \
  libssl-dev \
  libsqlite3-dev \
  dpkg-dev rpm \
  cppcheck \
  && rm -rf /var/lib/apt/lists/*

# Install LLVM/Clang (try versions from newest to oldest)
RUN apt-get update && \
  ( apt-get install -y clang-21 clang-tools-21 clang-tidy-21 llvm-21 llvm-21-dev lld-21 2>/dev/null && LLVM_VERSION=21 || \
    apt-get install -y clang-20 clang-tools-20 clang-tidy-20 llvm-20 llvm-20-dev lld-20 2>/dev/null && LLVM_VERSION=20 || \
    apt-get install -y clang-19 clang-tools-19 clang-tidy-19 llvm-19 llvm-19-dev lld-19 2>/dev/null && LLVM_VERSION=19 || \
    apt-get install -y clang-18 clang-tools-18 clang-tidy-18 llvm-18 llvm-18-dev lld-18 2>/dev/null && LLVM_VERSION=18 ) && \
  rm -rf /var/lib/apt/lists/*

# Set up LLVM as default compiler and clang-tidy
RUN LLVM_VERSION=$(ls /usr/lib/llvm-* -d 2>/dev/null | sort -V | tail -1 | grep -oE '[0-9]+$') && \
  LLVM_BIN="/usr/lib/llvm-${LLVM_VERSION}/bin" && \
  update-alternatives --install /usr/bin/clang clang ${LLVM_BIN}/clang 200 && \
  update-alternatives --install /usr/bin/clang++ clang++ ${LLVM_BIN}/clang++ 200 && \
  if [ -f /usr/bin/clang-tidy-${LLVM_VERSION} ]; then \
    update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-${LLVM_VERSION} 200; \
  fi

# Set compiler environment variables
ENV CC=clang \
    CXX=clang++ \
    CMAKE_GENERATOR=Ninja

WORKDIR /build

# Copy source code
COPY . /build/

# Initialize git submodules
RUN git submodule init && git submodule update --recursive

# Build ascii-chat in Release mode and install to /usr/local
# Disable defer tool (architecture compatibility issues in Docker)
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DASCIICHAT_ENABLE_DEFER_TRANSFORM=OFF \
    -GNinja && \
    cmake --build build -j$(nproc) && \
    cmake --install build

# ============================================================================
# Stage 2: Runtime
# ============================================================================
FROM ubuntu:24.04

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install only runtime dependencies (no development headers)
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Runtime libraries
    libzstd1 zlib1g libsodium23 \
    libportaudio2 libopus0 \
    libsqlite3-0 \
    liburcu8 \
    libprotobuf-c1 \
    libssl3t64 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy installed files from builder
COPY --from=builder /usr/local /usr/local

# Create non-root user for running ascii-chat
RUN useradd -m -u 1000 -s /bin/bash ascii && \
    mkdir -p /home/ascii/.config /home/ascii/.local/share && \
    chown -R ascii:ascii /home/ascii

# Switch to non-root user
USER ascii
WORKDIR /home/ascii

# Expose default ports
# 27224: ascii-chat server
# 27225: ACDS discovery service
EXPOSE 27224 27225

# Set ascii-chat as the entrypoint
# This allows users to run: docker run ghcr.io/zfogg/ascii-chat:latest server --port 8080
ENTRYPOINT ["/usr/local/bin/ascii-chat"]

# Default command shows help
CMD ["--help"]
