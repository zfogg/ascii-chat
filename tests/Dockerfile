# Dockerfile for running ASCII-Chat tests on Windows via Docker
# Uses Arch Linux for a lightweight container with full Criterion compatibility

FROM archlinux:latest

# Initialize pacman keyring (required for package verification)
RUN pacman-key --init && pacman-key --populate archlinux

# Use Clang as the default compiler
ENV CC=clang \
    CXX=clang++ \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    TESTING=1

# Install build dependencies and test framework
# Using packages from README.md Arch instructions
# Note: Using -S (not -Syu) to avoid upgrading base system packages
RUN pacman -Sy --noconfirm \
    pkg-config \
    clang \
    cmake \
    ninja \
    v4l-utils \
    zlib \
    portaudio \
    libsodium \
    criterion \
    bc \
    && pacman -Scc --noconfirm \
    && rm -rf /var/cache/pacman/pkg/* /tmp/* /var/tmp/*

# Set working directory
WORKDIR /app

# Don't copy or build in the image - we'll mount as volume
# This makes the image smaller and faster to build
# The build will be handled by run_tests.sh with proper incremental support

# Default command runs the test script directly
CMD ["./tests/scripts/run_tests.sh"]
