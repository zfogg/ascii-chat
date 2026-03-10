# Multi-stage Dockerfile for ascii-chat production builds
# Supports multi-arch: linux/amd64, linux/arm64

# ============================================================================
# Stage 1: Builder
# ============================================================================
FROM ubuntu:24.04 AS builder

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

WORKDIR /build

# Copy source code
COPY . /build/
COPY ./scripts/install-deps.sh /tmp/install-deps.sh

# Set up locale and minimal prerequisites
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && \
    apt-get install -y --no-install-recommends locales curl gpg ca-certificates && \
    localedef -i en_US -f UTF-8 en_US.UTF-8 && \
    rm -rf /var/lib/apt/lists/*

# Run the install-deps.sh script (handles platform-specific dependencies)
RUN chmod +x /tmp/install-deps.sh && /tmp/install-deps.sh

# Install yyjson via Homebrew
RUN useradd -m -s /bin/bash linuxbrew && \
    echo 'linuxbrew ALL=(ALL) NOPASSWD:ALL' >>/etc/sudoers

USER linuxbrew
RUN sh -c "$(curl -fsSL https://raw.githubusercontent.com/Linuxbrew/install/master/install.sh)"
RUN /home/linuxbrew/.linuxbrew/bin/brew install yyjson

USER root
ENV PATH="/home/linuxbrew/.linuxbrew/bin:${PATH}"

# Set compiler environment variables
ENV CC=clang \
    CXX=clang++ \
    CMAKE_GENERATOR=Ninja

# Build ascii-chat in Release mode and install to /usr/local
# Disable defer tool and analyzers (to speed up emulated builds)
RUN make install CMAKE_BUILD_TYPE=Release CMAKE_INSTALL_PREFIX=/usr/local EXTRA_CMAKE_ARGS="-DUSE_MUSL=OFF -DASCIICHAT_ENABLE_ANALYZERS=OFF"

# ============================================================================
# Stage 2: Runtime
# ============================================================================
FROM ubuntu:24.04

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

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
