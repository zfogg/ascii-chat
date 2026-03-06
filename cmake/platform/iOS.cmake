# =============================================================================
# iOS Platform Configuration Module
# =============================================================================
# This module handles iOS cross-compilation setup.
#
# Options:
#   - BUILD_IOS:       Build for iOS device (arm64)
#   - BUILD_IOS_SIM:   Build for iOS Simulator (arm64)
#
# These are cache options, not presets. Usage:
#   cmake --preset default -B build-ios -DBUILD_IOS=ON
#   cmake --preset default -B build-ios-sim -DBUILD_IOS_SIM=ON
#
# The toolchain is configured directly in CMakeLists.txt before project().
# =============================================================================

option(BUILD_IOS "Build for iOS device (arm64)" OFF)
option(BUILD_IOS_SIM "Build for iOS Simulator (arm64)" OFF)

# Configure cross-compilation settings if building for iOS
if(BUILD_IOS OR BUILD_IOS_SIM)
    set(PLATFORM_IOS TRUE)
    set(PLATFORM_POSIX TRUE)
    set(PLATFORM_DARWIN TRUE)
    set(CMAKE_CROSSCOMPILING TRUE)
    set(CMAKE_SYSTEM_NAME iOS)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "16.0")
    set(CMAKE_OSX_ARCHITECTURES "arm64")
    set(CMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE NO)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)
    set(BUILD_EXECUTABLES OFF)

    if(BUILD_IOS_SIM)
        set(CMAKE_OSX_SYSROOT "iphonesimulator")
        message(STATUS "Platform: iOS Simulator (arm64)")
    else()
        message(STATUS "Platform: iOS Device (arm64)")
    endif()
endif()
