# Multi-stage Dockerfile for ascii-chat release builds (static musl binary)
# Supports multi-arch: linux/amd64, linux/arm64
# Requires dependencies image: docker pull zfogg/ascii-chat-deps:latest
# Build with: DOCKER_BUILDKIT=1 docker build -f docker/release.dockerfile -t zfogg/ascii-chat:release .

# ============================================================================
# Stage 1: Builder (based on deps image with all build tools)
# ============================================================================
FROM zfogg/ascii-chat-deps:latest AS builder

# Ensure we have the environment set (inherited from deps image)
ENV SOURCE_COMMIT="docker-build"

# Submodules are baked into the deps image — no git clone needed at build time.
# Preserve submodules from deps image before COPY overwrites them
RUN mv /build/deps /tmp/deps-preserve

# Copy source code (.dockerignore excludes .deps-cache/ so pre-cached deps from
# the deps image are preserved)
COPY . /build/

# Restore submodules from deps image (overwrite empty dirs from build context)
RUN rm -rf /build/deps/ascii-chat-deps /build/deps/doxygen-awesome-css && \
    mv /tmp/deps-preserve/* /build/deps/ && rm -rf /tmp/deps-preserve

# Build ascii-chat (Release build with musl static linking)
RUN make CMAKE_BUILD_TYPE=Release EXTRA_CMAKE_ARGS="-DUSE_MUSL=ON -DASCIICHAT_ENABLE_ANALYZERS=OFF -DASCIICHAT_LIB_VERSION=0.3.0"

# Install to /usr/local (will be minimal since statically linked)
RUN make install CMAKE_BUILD_TYPE=Release CMAKE_INSTALL_PREFIX=/usr/local

# ============================================================================
# Stage 2: Runtime (minimal - just the static binary and essentials)
# ============================================================================
FROM alpine:latest

# Prevent interactive prompts
ENV DOCKERFILE_DEBIAN_FRONTEND=noninteractive

# Install minimal runtime dependencies for static binary
# Alpine is lightweight; musl static binary needs only basics
RUN apk add --no-cache \
      ca-certificates \
      curl \
      wget \
      unzip \
      openssh-client \
      gpg

# Copy installed binary and libraries from builder (static binary)
COPY --from=builder /usr/local/bin/ascii-chat /usr/local/bin/ascii-chat
COPY --from=builder /usr/local/bin/acds /usr/local/bin/acds

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
RUN adduser -D -u 1001 -s /bin/sh ascii && \
    mkdir -p /home/ascii/.config /home/ascii/.local/share && \
    chown -R ascii:ascii /home/ascii && \
    chown -R ascii:ascii /acds 2>/dev/null || true

# Switch to non-root user
USER ascii
WORKDIR /home/ascii

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
