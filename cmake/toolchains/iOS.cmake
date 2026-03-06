# =============================================================================
# iOS CMake Toolchain
# =============================================================================
# This toolchain configures CMake for cross-compilation to iOS.
# It sets the necessary flags for arm64 iOS and iOS Simulator.
#
# Usage:
#   iOS device:
#     cmake --preset default -B build-ios -DBUILD_IOS=ON
#   iOS simulator:
#     cmake --preset default -B build-ios-sim -DBUILD_IOS_SIM=ON
#
# The iOS.cmake toolchain is NOT used directly. Instead, the BUILD_IOS/BUILD_IOS_SIM
# options in CMakeLists.txt configure cross-compilation settings directly.
# =============================================================================

set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_OSX_DEPLOYMENT_TARGET "16.0")
set(CMAKE_OSX_ARCHITECTURES "arm64")
set(CMAKE_CROSSCOMPILING TRUE)

# Use Clang (project default)
set(CMAKE_C_COMPILER_TARGET arm-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET})

# Bitcode is deprecated in Xcode 14+
set(CMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE NO)

# Framework search paths
set(CMAKE_FIND_FRAMEWORK FIRST)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
