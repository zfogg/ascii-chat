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
    set(PLATFORM_IOS TRUE CACHE BOOL "Build for iOS" FORCE)
    set(PLATFORM_POSIX TRUE CACHE BOOL "iOS uses POSIX" FORCE)
    set(PLATFORM_DARWIN TRUE CACHE BOOL "iOS is Darwin-based" FORCE)
    set(CMAKE_CROSSCOMPILING TRUE CACHE BOOL "iOS is cross-compile" FORCE)
    set(CMAKE_SYSTEM_NAME iOS CACHE STRING "iOS system name" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "16.0" CACHE STRING "iOS deployment target" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "iOS architecture" FORCE)
    set(CMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE NO CACHE BOOL "Disable bitcode" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "iOS uses static libs" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "iOS disables tests" FORCE)
    set(BUILD_EXECUTABLES OFF CACHE BOOL "iOS disables executables" FORCE)

    # Set centralized iOS dependencies cache directory
    # Used by all iOS dependencies (OpenSSL, BearSSL, zstd, yyjson, libsodium, FreeType, libvterm, WebRTC, etc.)
    # Note: ASCIICHAT_DEPS_CACHE_DIR already includes CMAKE_BUILD_TYPE, so just append /ios
    set(IOS_DEPS_CACHE_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/ios" CACHE PATH "iOS deps cache" FORCE)
    file(MAKE_DIRECTORY "${IOS_DEPS_CACHE_DIR}")

    if(BUILD_IOS_SIM)
        set(CMAKE_OSX_SYSROOT "iphonesimulator")
        set(IOS_BUILD_VARIANT "sim")
        message(STATUS "Platform: iOS Simulator (arm64)")
    else()
        set(CMAKE_OSX_SYSROOT "iphoneos")
        set(IOS_BUILD_VARIANT "device")
        message(STATUS "Platform: iOS Device (arm64)")
    endif()

    message(STATUS "iOS deps cache: ${IOS_DEPS_CACHE_DIR}")
endif()
