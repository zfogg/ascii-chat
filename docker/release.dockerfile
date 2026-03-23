# vim: set ft=dockerfile:
# Multi-stage Dockerfile for ascii-chat release builds (static musl binary)
# Build with: DOCKER_BUILDKIT=1 docker build -f docker/release.dockerfile -t zfogg/ascii-chat:release .
# Set version: DOCKER_BUILDKIT=1 docker build --build-arg VERSION=0.9.13 -f docker/release.dockerfile -t zfogg/ascii-chat:0.9.13 .

# ============================================================================
# Stage 1: Builder (deps image with all build tools and cached dependencies)
# ============================================================================
FROM zfogg/ascii-chat-deps:latest AS builder

# Accept VERSION build arg in builder stage
ARG VERSION=0.0.0

ENV SOURCE_COMMIT="docker-build"

# Preserve submodules from deps image
RUN mv /build/deps /tmp/deps-preserve

# Copy fresh source (excludes .deps-cache/ via .dockerignore)
COPY . /build/

# Restore submodules
RUN rm -rf /build/deps/ascii-chat-deps /build/deps/doxygen-awesome-css && \
    mv /tmp/deps-preserve/* /build/deps/ && rm -rf /tmp/deps-preserve

# Build release-musl static binary - reuse pre-configured build from deps stage
# Create minimal .git structure so Ninja/CMake dependencies are satisfied
# Clean release builds show just version without dev/commit info
RUN cd /build && \
    mkdir -p .git && \
    touch .git/HEAD .git/index && \
    PROJECT_VERSION_OVERRIDE=${VERSION} \
    SOURCE_COMMIT="docker-release" \
    cmake --build build_release && \
    cmake --install build_release --prefix /tmp/install && \
    strip /tmp/install/bin/ascii-chat

# ============================================================================
# Stage 2: Runtime (minimal Alpine for static binary)
# ============================================================================
FROM alpine:latest

ENV DOCKERFILE_DEBIAN_FRONTEND=noninteractive

# Install only what's needed for static binary
RUN apk add --no-cache \
      ca-certificates curl wget unzip openssh-client gpg bash

# Copy only the static binary - no dependencies needed
COPY --from=builder /tmp/install/bin/ascii-chat /usr/local/bin/ascii-chat

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

# Create non-root user
RUN adduser -D -u 1001 -s /bin/sh ascii && \
    mkdir -p /home/ascii/.config /home/ascii/.local/share && \
    chown -R ascii:ascii /home/ascii && \
    chown -R ascii:ascii /acds 2>/dev/null || true

# Create data directory for ACDS database (volume mount point)
RUN mkdir -p /data && chown ascii:ascii /data

USER ascii
WORKDIR /home/ascii

EXPOSE 27224 27225 27226 27227 443

ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
CMD ["--help"]
