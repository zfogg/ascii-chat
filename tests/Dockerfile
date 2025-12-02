# Dockerfile for running ascii-chat tests on Windows via Docker
# Uses Arch Linux for a lightweight container with full Criterion compatibility

FROM archlinux:latest

# Initialize pacman keyring (required for package verification)
RUN pacman-key --init && pacman-key --populate archlinux

# Use Clang as the default compiler, Ninja as default generator
ENV CC=clang \
  CXX=clang++ \
  CMAKE_GENERATOR=Ninja \
  LANG=C.UTF-8 \
  LC_ALL=C.UTF-8 \
  TESTING=1 \
  ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer

# Configure ccache for Docker volume caching
# CCACHE_DIR: Cache directory (will be mounted as volume)
# CCACHE_UMASK: Ensure cache files are accessible
# CCACHE_HARDLINK: Use hard links to save space
# CCACHE_NLEVELS: Hash directory levels (default 2 is optimal)
ENV CCACHE_DIR=/ccache \
  CCACHE_UMASK=002 \
  CCACHE_HARDLINK=true \
  CCACHE_NLEVELS=2

# Install git first (needed for install-deps.sh to work)
RUN pacman -Sy --noconfirm git

# Copy install-deps.sh and run it with modifications for Docker:
# - Skip git submodule commands (source mounted at runtime, submodules on host)
# - Add --noconfirm for non-interactive pacman installs
# - Remove sudo (running as root in Docker)
COPY scripts/install-deps.sh /tmp/install-deps.sh
RUN chmod +x /tmp/install-deps.sh && \
    sed -i 's/^git submodule/#git submodule/g' /tmp/install-deps.sh && \
    sed -i 's/sudo pacman -S --needed/pacman -S --noconfirm --needed/g' /tmp/install-deps.sh && \
    /tmp/install-deps.sh

# Install additional packages needed for Docker testing/packaging
# These are not in install-deps.sh but required for the test environment
RUN pacman -S --noconfirm --needed \
  which \
  && pacman -Scc --noconfirm \
  && rm -rf /var/cache/pacman/pkg/* /tmp/* /var/tmp/*

# Set working directory
WORKDIR /app

# Default command builds and runs tests with ctest
CMD ["sh", "-c", "cmake --preset docker && cmake --build build_docker --target tests && ctest --test-dir build_docker --output-on-failure --parallel 0"]
