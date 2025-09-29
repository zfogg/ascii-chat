# Dockerfile for running ASCII-Chat tests on Windows via Docker
# Uses Ubuntu as base to ensure full Criterion compatibility

FROM ubuntu:25.10

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Set CMAKE_PREFIX_PATH for package discovery
ENV CMAKE_PREFIX_PATH=/usr

# Use Clang as the default compiler
ENV CC=clang
ENV CXX=clang++

# Set UTF-8 locale for proper Unicode support
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

# Install build dependencies and test framework
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    clang \
    clang-format \
    clang-tidy \
    clang-tools \
    llvm \
    libcriterion-dev \
    locales \
    libportaudio2 \
    portaudio19-dev \
    zlib1g-dev \
    libsodium-dev \
    libncurses-dev \
    libncurses5-dev \
    ncurses-dev \
    libprotobuf-c-dev \
    libnanomsg-dev \
    libgit2-dev \
    libssh2-1-dev \
    libhttp-parser-dev \
    libpcre2-dev \
    pkg-config \
    git \
    bc \
    grc \
    && rm -rf /var/lib/apt/lists/* \
    && ln -sf /usr/lib/x86_64-linux-gnu/libncurses.so.6 /usr/lib/x86_64-linux-gnu/libcurses.so

# Set working directory
WORKDIR /app

# Don't copy or build in the image - we'll mount as volume
# This makes the image smaller and faster to build
# The build will be handled by run_tests.sh with proper incremental support

# Default command runs the test script directly
CMD ["./tests/scripts/run_tests.sh"]
