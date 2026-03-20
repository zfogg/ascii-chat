# Dependencies stage for ascii-chat
# Builds the development environment with all build dependencies
# Build with: docker build -f docker/deps.dockerfile -t zfogg/ascii-chat-deps:latest .
# Push with: docker push zfogg/ascii-chat-deps:latest

FROM ubuntu:24.04

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
