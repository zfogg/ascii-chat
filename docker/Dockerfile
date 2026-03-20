# Multi-stage Dockerfile for ascii-chat production builds
# Supports multi-arch: linux/amd64, linux/arm64
# Requires dependencies image: docker pull zfogg/ascii-chat-deps:latest
# Build with: DOCKER_BUILDKIT=1 docker build -f docker/Dockerfile -t zfogg/ascii-chat .

# ============================================================================
# Stage 1: Builder (based on deps image with all build tools)
# ============================================================================
FROM zfogg/ascii-chat-deps:latest AS builder

# Ensure we have the environment set (inherited from deps image)
ENV SOURCE_COMMIT="docker-build"

# Copy remaining source code (changes here only invalidate build step cache)
COPY . /build/

# Build ascii-chat (Dev build without sanitizers for Docker runtime compatibility)
RUN rm -f /build/build/CMakeCache.txt /build/build/build.ninja && \
    make CMAKE_BUILD_TYPE=Dev EXTRA_CMAKE_ARGS="-DUSE_MUSL=OFF -DASCIICHAT_ENABLE_ANALYZERS=OFF -DASCIICHAT_LIB_VERSION=0.3.0"

# Install to /usr/local
RUN make install CMAKE_BUILD_TYPE=Dev CMAKE_INSTALL_PREFIX=/usr/local

# ============================================================================
# Stage 2: Runtime
# ============================================================================
FROM ubuntu:24.04

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Set up locale and minimal runtime dependencies
RUN apt-get update && \
    apt-get install -y --no-install-recommends locales ca-certificates && \
    localedef -i en_US -f UTF-8 en_US.UTF-8 && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

# Install minimal runtime dependencies for dynamically-linked Dev build
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      ca-certificates curl unzip openssh-client gpg \
      libportaudio2 libopus0 libsodium23 libzstd1 \
      libssl3 libfreetype6 libfontconfig1 libvterm0 && \
    apt-get clean && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/* /var/tmp/*

# Copy installed files from builder
COPY --from=builder /usr/local /usr/local
COPY --from=builder /home/linuxbrew/.linuxbrew /home/linuxbrew/.linuxbrew
# Copy all system libraries from builder to get all runtime dependencies
COPY --from=builder /usr/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu

# Copy entrypoint script
COPY docker/docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
RUN chmod +x /usr/local/bin/docker-entrypoint.sh

# Create ACDS directory for keys
RUN mkdir -p /acds && chmod 755 /acds

# Copy SSH and GPG keys from build secrets
RUN --mount=type=secret,id=KEY_SSH,target=/run/secrets/KEY_SSH \
    --mount=type=secret,id=KEY_GPG,target=/run/secrets/KEY_GPG \
    cp /run/secrets/KEY_SSH /acds/key 2>/dev/null || true && \
    chmod 600 /acds/key 2>/dev/null || true && \
    cp /run/secrets/KEY_GPG /acds/key.gpg 2>/dev/null || true && \
    chmod 600 /acds/key.gpg 2>/dev/null || true

# Create non-root user for running ascii-chat
RUN useradd -m -u 1001 -s /bin/bash ascii && \
    mkdir -p /home/ascii/.config /home/ascii/.local/share && \
    chown -R ascii:ascii /home/ascii && \
    chown -R ascii:ascii /acds 2>/dev/null || true

# Switch to non-root user
USER ascii
WORKDIR /home/ascii

# Set library path to include Homebrew libraries and local lib directory
ENV LD_LIBRARY_PATH="/home/linuxbrew/.linuxbrew/lib:/usr/local/lib:/usr/lib/x86_64-linux-gnu"

# Expose default ports
# 27224: ascii-chat server
# 27225: ACDS discovery service
EXPOSE 27224 27225 27226 27227 443

# Set entrypoint script (generates keys if not provided, then runs ascii-chat)
# Users can override the command in their deployment platform (Coolify, etc)
# Example: discovery-service 0.0.0.0 :: --key /acds/key
ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]

# Default command shows help
CMD ["--help"]
