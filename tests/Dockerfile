# Dockerfile for running ASCII-Chat tests on Windows via Docker
# Uses Ubuntu as base to ensure full Criterion compatibility

FROM ubuntu:22.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Set CMAKE_PREFIX_PATH for package discovery
ENV CMAKE_PREFIX_PATH=/usr

# Use Clang as the default compiler
ENV CC=clang
ENV CXX=clang++

# Install build dependencies and test framework
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    clang \
    libcriterion-dev \
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
    && rm -rf /var/lib/apt/lists/* \
    && ln -sf /usr/lib/x86_64-linux-gnu/libncurses.so.6 /usr/lib/x86_64-linux-gnu/libcurses.so

# Set working directory
WORKDIR /app

# Don't copy or build in the image - we'll mount as volume
# This makes the image smaller and faster to build

# Create a script to ensure tests are built before running using CMake
RUN echo '#!/bin/bash\n\
if [ ! -d "build" ] || [ "$1" = "--rebuild" ]; then\n\
    echo "Building tests with CMake..."\n\
    rm -rf build\n\
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON\n\
    cmake --build build\n\
fi\n\
shift\n\
exec ./tests/scripts/run_tests.sh "$@"' > /usr/local/bin/run-tests.sh && \
    chmod +x /usr/local/bin/run-tests.sh

# Default command runs the test wrapper
CMD ["/usr/local/bin/run-tests.sh"]