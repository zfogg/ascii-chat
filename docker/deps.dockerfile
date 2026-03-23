# vim: set ft=dockerfile:
# Dependencies stage for ascii-chat
# Builds the development environment with all build dependencies and pre-downloads
# CMake FetchContent deps into .deps-cache/ so the main Dockerfile skips downloads.
#
# Build with: docker build -f docker/deps.dockerfile -t zfogg/ascii-chat-deps:latest .
# Push with: docker push zfogg/ascii-chat-deps:latest

FROM ubuntu:24.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

WORKDIR /build

# Copy install-deps script first (changes rarely, good cache layer)
COPY scripts/install-deps.sh /tmp/install-deps.sh

# Set up locale and minimal prerequisites
RUN apt-get update && \
    apt-get install -y --no-install-recommends locales curl wget git gpg ca-certificates lsb-release software-properties-common && \
    localedef -i en_US -f UTF-8 en_US.UTF-8

# Run install-deps.sh
RUN chmod +x /tmp/install-deps.sh && /tmp/install-deps.sh && \
    apt-get clean && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/* /var/tmp/*

# Install yyjson via Homebrew
RUN useradd -m -s /bin/bash linuxbrew && \
    echo 'linuxbrew ALL=(ALL) NOPASSWD:ALL' >>/etc/sudoers

USER linuxbrew
RUN sh -c "$(curl -fsSL https://raw.githubusercontent.com/Linuxbrew/install/master/install.sh)"
RUN /home/linuxbrew/.linuxbrew/bin/brew install yyjson

USER root

# Set compiler and library path environment variables
ENV PATH="/home/linuxbrew/.linuxbrew/bin:${PATH}" \
    LD_LIBRARY_PATH="/home/linuxbrew/.linuxbrew/lib:/build/build/lib" \
    CC=clang \
    CXX=clang++ \
    CMAKE_GENERATOR=Ninja

# Clone submodules (after COPY so they aren't overwritten by empty dirs from build context)
RUN rm -rf /build/deps/ascii-chat-deps /build/deps/doxygen-awesome-css && \
    git clone --depth 1 https://github.com/zfogg/BearSSL.git /build/deps/ascii-chat-deps/bearssl && \
    git clone --depth 1 https://github.com/cktan/tomlc17 /build/deps/ascii-chat-deps/tomlc17 && \
    git clone --depth 1 https://github.com/zfogg/libsodium-bcrypt-pbkdf.git /build/deps/ascii-chat-deps/libsodium-bcrypt-pbkdf && \
    git clone --depth 1 https://github.com/floooh/sokol /build/deps/ascii-chat-deps/sokol && \
    git clone --depth 1 https://github.com/jothepro/doxygen-awesome-css.git /build/deps/doxygen-awesome-css && \
    git clone --depth 1 https://github.com/mjansson/mdns.git /build/deps/ascii-chat-deps/mdns && \
    git clone --depth 1 https://github.com/troydhanson/uthash.git /build/deps/ascii-chat-deps/uthash && \
    git clone --depth 1 https://github.com/JuliaStrings/utf8proc.git /build/deps/ascii-chat-deps/utf8proc && \
    git clone --depth 1 https://github.com/strukturag/libde265.git /build/deps/ascii-chat-deps/libde265 && \
    git clone --depth 1 https://github.com/videolan/x265.git /build/deps/ascii-chat-deps/x265 && \
    git clone --depth 1 https://github.com/ibireme/yyjson.git /build/deps/ascii-chat-deps/yyjson && \
    rm -rf /build/deps/ascii-chat-deps/*/.git /build/deps/doxygen-awesome-css/.git

# Copy full source tree needed for cmake configure (downloads FetchContent deps).
# .dockerignore excludes .deps-cache/, build/, .git, tests/, etc.
COPY . /build/

# In production this is set by a script in scripts/update-coolify-version.zsh
ENV LIB_VERSION=0.3.0

# Run cmake configure to populate .deps-cache/ with all FetchContent downloads.
# Configures both Debug and Release presets.
# After configure, remove the build/ artifacts (CMakeCache, ninja files, object files)
# but keep .deps-cache/ which persists outside build/ (shared across all build types).
RUN cmake --preset default \
        -DASCIICHAT_ENABLE_ANALYZERS=OFF \
        -DASCIICHAT_LIB_VERSION=${LIB_VERSION} && \
    rm -rf build/

# Configure Release preset with musl for static linking.
# This shares the same .deps-cache/ as the Debug build above.
# Keep build_release/ directory so release.dockerfile can reuse it without reconfiguring
RUN cmake --preset release-musl \
        -DUSE_MUSL=ON \
        -DASCIICHAT_ENABLE_ANALYZERS=OFF \
        -DASCIICHAT_LIB_VERSION=${LIB_VERSION}

