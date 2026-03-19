# Multi-stage Dockerfile for ascii-chat production builds
# Supports multi-arch: linux/amd64, linux/arm64
# Build with: DOCKER_BUILDKIT=1 docker build . -t zfogg/ascii-chat

# ============================================================================
# Stage 1: Builder
# ============================================================================
FROM ubuntu:24.04 AS builder

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

WORKDIR /build

# Copy only deps-related files first (these change rarely)
COPY scripts/install-deps.sh /tmp/install-deps.sh
COPY CMakeLists.txt Makefile /build/
COPY cmake/ /build/cmake/

# Set up locale and minimal prerequisites
RUN apt-get update && \
    apt-get install -y --no-install-recommends locales curl wget git gpg ca-certificates lsb-release software-properties-common && \
    localedef -i en_US -f UTF-8 en_US.UTF-8

# Run install-deps.sh
RUN chmod +x /tmp/install-deps.sh && /tmp/install-deps.sh && \
    apt-get clean && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/* /var/tmp/*

# Install yyjson via Homebrew with cache mount
RUN useradd -m -s /bin/bash linuxbrew && \
    echo 'linuxbrew ALL=(ALL) NOPASSWD:ALL' >>/etc/sudoers

USER linuxbrew
RUN sh -c "$(curl -fsSL https://raw.githubusercontent.com/Linuxbrew/install/master/install.sh)"
RUN /home/linuxbrew/.linuxbrew/bin/brew install yyjson

USER root
ENV PATH="/home/linuxbrew/.linuxbrew/bin:${PATH}" \
    LD_LIBRARY_PATH="/home/linuxbrew/.linuxbrew/lib:/build/build/lib" \
    SOURCE_COMMIT="docker-build"

# Set compiler environment variables
ENV CC=clang \
    CXX=clang++ \
    CMAKE_GENERATOR=Ninja

# Copy remaining source code (changes here only invalidate build step cache)
COPY . /build/

# Build ascii-chat
RUN rm -f /build/build/CMakeCache.txt /build/build/build.ninja && \
    make CMAKE_BUILD_TYPE=Debug EXTRA_CMAKE_ARGS="-DUSE_MUSL=OFF -DASCIICHAT_ENABLE_ANALYZERS=OFF -DASCIICHAT_LIB_VERSION=0.3.0"

# Install to /usr/local
RUN make install CMAKE_BUILD_TYPE=Debug CMAKE_INSTALL_PREFIX=/usr/local

# ============================================================================
# Stage 2: Runtime
# ============================================================================
FROM ubuntu:24.04

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Set up locale
RUN apt-get update && \
    apt-get install -y --no-install-recommends locales ca-certificates && \
    localedef -i en_US -f UTF-8 en_US.UTF-8 && \
    rm -rf /var/lib/apt/lists/*

# Install runtime dependencies via install-deps.sh
COPY ./scripts/install-deps.sh /tmp/install-deps.sh
RUN chmod +x /tmp/install-deps.sh && /tmp/install-deps.sh

# Copy installed files from builder
COPY --from=builder /usr/local /usr/local
COPY --from=builder /home/linuxbrew/.linuxbrew /home/linuxbrew/.linuxbrew

# Copy entrypoint script
COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
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

# Set library path to include Homebrew libraries from builder and local lib directory
ENV LD_LIBRARY_PATH="/home/linuxbrew/.linuxbrew/lib:/usr/local/lib:/usr/lib/x86_64-linux-gnu"

# Expose default ports
# 27224: ascii-chat server
# 27225: ACDS discovery service
EXPOSE 27224 27225

# Set entrypoint script (generates keys if not provided, then runs ascii-chat)
# Users can override the command in their deployment platform (Coolify, etc)
# Example: discovery-service 0.0.0.0 :: --key /acds/key
ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]

# Default command shows help
CMD ["--help"]
