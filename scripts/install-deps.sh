#!/usr/bin/env bash
# Install dependencies for ascii-chat
#
# This script handles platform-specific dependency installation:
# - macOS: Uses Homebrew
# - Linux: Uses apt-get, yum, or pacman
# - Windows: Directs to install-deps.ps1
#
# Usage:
#   ./install-deps.sh           # Install Debug dependencies
#   ./install-deps.sh -Release  # Install Release dependencies (static libraries)
#
# Note: On Unix systems, -dev packages include both static (.a) and dynamic (.so) libraries.
#       The actual static/dynamic linking is controlled by CMake based on build type.

set -e

# Parse arguments
CONFIG="Debug"
if [[ "$1" == "-Release" ]] || [[ "$1" == "--release" ]]; then
  CONFIG="Release"
fi

echo ""
echo "=== ascii-chat Dependency Installer ==="
echo ""
echo "Build configuration: $CONFIG"
if [[ "$CONFIG" == "Release" ]]; then
  echo "Note: Release builds will use static linking where available"
fi
echo ""

echo "Getting submodules"
git submodule init
git submodule update --recursive

# Detect platform
if [[ "$OSTYPE" == "darwin"* ]]; then
  PLATFORM="macos"
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
  PLATFORM="linux"
elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]]; then
  PLATFORM="windows"
else
  echo >&2 "ERROR: Unsupported platform: $OSTYPE"
  exit 1
fi

echo "Detected platform: $PLATFORM"
echo ""

# macOS: Use Homebrew
if [[ "$PLATFORM" == "macos" ]]; then
  if ! command -v brew &>/dev/null; then
    echo >&2 "ERROR: Homebrew not found"
    echo >&2 "Please install Homebrew from https://brew.sh"
    exit 1
  fi

  echo "Installing dependencies via Homebrew..."
  brew install cmake coreutils pkg-config llvm ccache make ninja mimalloc zstd libsodium portaudio criterion libxml2

  echo ""
  echo "Dependencies installed successfully!"
  if [[ "$CONFIG" == "Release" ]]; then
    echo "You can now run: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build"
  else
    echo "You can now run: cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build"
  fi

# Linux: Detect package manager
elif [[ "$PLATFORM" == "linux" ]]; then
  if command -v apt-get &>/dev/null; then
    echo "Detected apt-get package manager"
    echo "Installing dependencies..."

    # INFO: see https://apt.kitware.com/
    set +e
    sudo /bin/bash -c "$(curl -fsSL https://apt.kitware.com/kitware-archive.sh)" 2>/dev/null
    kitware_apt_sh_result="$?"
    set -e

    if [ $kitware_apt_sh_result -eq 1 ]; then
      sudo apt-get update
      sudo apt-get install ca-certificates gpg wget

      test -f /usr/share/doc/kitware-archive-keyring/copyright \
        || wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null \
        | gpg --dearmor - \
        | sudo tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null

      ubuntu_version_major="$(lsb_release -a | grep Release: | grep -Eo '[0-9]+' | head -n1)"
      local ubuntu_version_name_short=""
      if [ "$ubuntu_version_major" -eq "24" || "$ubuntu_version_major" -eq "25" ]; then
        local ubuntu_version_name_short="noble"
      elif [ "$ubuntu_version_major" -eq "23" ]; then
        local ubuntu_version_name_short="jammy"
      elif [ "$ubuntu_version_major" -eq "22" ]; then
        local ubuntu_version_name_short="focal"
      else
        echo >&2 "Unsupported Ubuntu version $ubuntu_version_major"
        exit 1
      fi

      echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ '"$ubuntu_version_name_short"' main' \
        | sudo tee /etc/apt/sources.list.d/kitware.list >/dev/null

      sudo apt-get update

      test -f /usr/share/doc/kitware-archive-keyring/copyright \
        || sudo rm /usr/share/keyrings/kitware-archive-keyring.gpg

      sudo apt-get install kitware-archive-keyring
    fi

    sudo apt-get update

    # Install non-LLVM dependencies first
    sudo apt-get install -y \
      cmake \
      ninja-build \
      pkg-config \
      musl-tools \
      musl-dev \
      libmimalloc-dev \
      libzstd-dev \
      zlib1g-dev \
      libsodium-dev \
      portaudio19-dev \
      libcriterion-dev \
      libffi-dev \
      doxygen \
      dpkg-dev

    # Try LLVM versions in order from newest to oldest
    LLVM_VERSIONS="21 20 19 18"
    LLVM_VERSION=""
    for ver in $LLVM_VERSIONS; do
      echo "Trying LLVM $ver..."
      if sudo apt-get install -y clang-$ver clang-tools-$ver libclang-$ver-dev llvm-$ver llvm-$ver-dev 2>/dev/null; then
        LLVM_VERSION=$ver
        echo "Successfully installed LLVM $ver"
        break
      else
        echo >&2 "LLVM $ver not available, trying next version..."
      fi
    done

    if [ -z "$LLVM_VERSION" ]; then
      echo >&2 "ERROR: Could not install any LLVM version ($LLVM_VERSIONS)"
      exit 1
    fi

    echo "Configuring LLVM $LLVM_VERSION as default compiler..."

    LLVM_BIN="/usr/lib/llvm-$LLVM_VERSION/bin"
    LLVM_TOOLS="clang clang++ clang-format clang-tidy lld ld.lld lldb llvm-config llvm-ar llvm-nm llvm-objdump llvm-ranlib llvm-symbolizer llvm-cov llvm-profdata"

    # Remove existing alternatives that might conflict (they may be registered as masters)
    # This allows us to set up fresh alternatives with our desired configuration
    for tool in $LLVM_TOOLS; do
      sudo update-alternatives --remove-all "$tool" 2>/dev/null || true
    done

    # Also remove any non-alternatives binaries in /usr/bin that might shadow our alternatives
    for tool in $LLVM_TOOLS; do
      if [ -e "/usr/bin/$tool" ] && [ ! -L "/usr/bin/$tool" ]; then
        echo "Removing non-symlink $tool from /usr/bin..."
        sudo rm -f "/usr/bin/$tool"
      elif [ -L "/usr/bin/$tool" ] && [ ! -e "/etc/alternatives/$tool" ]; then
        echo "Removing stale symlink $tool from /usr/bin..."
        sudo rm -f "/usr/bin/$tool"
      fi
    done

    # Register each tool as a separate alternative (no slaves - avoids conflicts)
    # Use priority 200 to override any lower-priority alternatives
    sudo update-alternatives --install /usr/bin/clang clang ${LLVM_BIN}/clang 200
    sudo update-alternatives --install /usr/bin/clang++ clang++ ${LLVM_BIN}/clang++ 200
    sudo update-alternatives --install /usr/bin/clang-format clang-format ${LLVM_BIN}/clang-format 200
    sudo update-alternatives --install /usr/bin/clang-tidy clang-tidy ${LLVM_BIN}/clang-tidy 200
    sudo update-alternatives --install /usr/bin/lld lld ${LLVM_BIN}/lld 200
    sudo update-alternatives --install /usr/bin/ld.lld ld.lld ${LLVM_BIN}/ld.lld 200
    sudo update-alternatives --install /usr/bin/lldb lldb ${LLVM_BIN}/lldb 200
    sudo update-alternatives --install /usr/bin/llvm-config llvm-config ${LLVM_BIN}/llvm-config 200
    sudo update-alternatives --install /usr/bin/llvm-ar llvm-ar ${LLVM_BIN}/llvm-ar 200
    sudo update-alternatives --install /usr/bin/llvm-nm llvm-nm ${LLVM_BIN}/llvm-nm 200
    sudo update-alternatives --install /usr/bin/llvm-objdump llvm-objdump ${LLVM_BIN}/llvm-objdump 200
    sudo update-alternatives --install /usr/bin/llvm-ranlib llvm-ranlib ${LLVM_BIN}/llvm-ranlib 200
    sudo update-alternatives --install /usr/bin/llvm-symbolizer llvm-symbolizer ${LLVM_BIN}/llvm-symbolizer 200
    sudo update-alternatives --install /usr/bin/llvm-cov llvm-cov ${LLVM_BIN}/llvm-cov 200
    sudo update-alternatives --install /usr/bin/llvm-profdata llvm-profdata ${LLVM_BIN}/llvm-profdata 200

    # Explicitly set the alternatives to ensure our version is active
    sudo update-alternatives --set clang ${LLVM_BIN}/clang
    sudo update-alternatives --set clang++ ${LLVM_BIN}/clang++
    sudo update-alternatives --set llvm-config ${LLVM_BIN}/llvm-config

    # Ensure apt-installed cmake in /usr/bin is used
    # GitHub runners have pre-installed cmake in /usr/local/bin that may take precedence
    # Remove any conflicting cmake from /usr/local/bin if it exists
    if [ -f /usr/local/bin/cmake ]; then
      echo >&2 "Removing old cmake from /usr/local/bin..."
      sudo rm -f /usr/local/bin/cmake
    fi

    # Verify configuration
    echo "Verifying LLVM $LLVM_VERSION configuration:"
    clang --version | head -1
    clang++ --version | head -1
    llvm-config --version
    echo "Verifying CMake configuration:"
    which cmake
    cmake --version | head -1

  elif command -v yum &>/dev/null; then
    echo "Detected yum package manager"
    echo "Installing dependencies..."
    sudo yum install -y \
      clang \
      llvm \
      musl-devel \
      cmake \
      ninja-build \
      pkg-config \
      musl-gcc \
      musl-libc-static \
      mimalloc-devel \
      libzstd-devel \
      zlib-devel \
      libsodium-devel \
      portaudio-devel \
      jack-audio-connection-kit-devel \
      criterion-devel \
      libffi-devel \
      doxygen \
      rpm-build

  elif command -v pacman &>/dev/null; then
    echo "Detected pacman package manager"
    echo "Installing dependencies..."
    sudo pacman -S --needed \
      pkg-config \
      clang llvm lldb ccache \
      cmake ninja make \
      musl mimalloc \
      zstd zlib libsodium portaudio \
      criterion libffi

  else
    echo >&2 "ERROR: No supported package manager found (apt-get, yum, or pacman)"
    echo >&2 "Please install dependencies manually:"
    echo >&2 "  - pkg-config"
    echo >&2 "  - llvm (the binary tools and runtime libraries)"
    echo >&2 "  - * zlib (library and development headers. * this is an llvm dependency - you may already have it installed)"
    echo >&2 "  - clang (both clang and clang++)"
    echo >&2 "  - * musl (development tools. * this is only needed if you plan to do a release build)"
    echo >&2 "  - * mimalloc (library and development headers. * this is only needed if you plan to do a release build)"
    echo >&2 "  - zstd (library and development headers)"
    echo >&2 "  - libsodium (library and development headers)"
    echo >&2 "  - portaudio (library and development headers)"
    echo >&2 "  - criterion (testing framework, library and development headers)"
    echo >&2 "  - libffi (foreign function interface, required by criterion)"
    echo >&2 "  - * jack (library and development headers. * you might need this - on some Linux systems, the Portaudio build from the system package repos is linked to Jack but doesn't list Jack as a dependency so it won't be automatically installed and builds will fail without it)"
    exit 1
  fi

  echo ""
  echo "Dependencies installed successfully!"
  if [[ "$CONFIG" == "Release" ]]; then
    echo "You can now run: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build"
  else
    echo "You can now run: cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build"
  fi

# Windows: Direct to PowerShell script
elif [[ "$PLATFORM" == "windows" ]]; then
  echo "On Windows, please use the PowerShell script instead:"
  if [[ "$CONFIG" == "Release" ]]; then
    echo "  ./scripts/install-deps.ps1 -Release"
  else
    echo "  ./scripts/install-deps.ps1"
  fi
  exit 1
fi
