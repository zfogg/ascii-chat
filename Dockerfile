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

# Initialize git submodules
COPY ./scripts/install-deps.sh /tmp/install-deps.sh
RUN chmod +x /tmp/install-deps.sh && /tmp/install-deps.sh && rm -rf /var/lib/apt/lists/*

# Set compiler environment variables
ENV CC=clang \
    CXX=clang++ \
    CMAKE_GENERATOR=Ninja

# Build ascii-chat in Release mode and install to /usr/local
# Disable defer tool and analyzers (to speed up emulated builds)
RUN make install

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
