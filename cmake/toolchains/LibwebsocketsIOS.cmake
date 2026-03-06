# =============================================================================
# libwebsockets iOS Cross-Compilation Toolchain
# =============================================================================
# This toolchain file enables cross-compilation of libwebsockets for iOS
# Based on libwebsockets' official iOS.cmake with ascii-chat customizations
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=path/to/LibwebsocketsIOS.cmake \
#         -DIOS_PLATFORM=OS|SIMULATOR64 \
#         -DIOS_DEPLOYMENT_TARGET=16.0 \
#         ...
#
# =============================================================================

# Platform detection - must be set before project()
set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_VERSION ${IOS_DEPLOYMENT_TARGET})

# Get iOS platform variant (device vs simulator)
if(NOT IOS_PLATFORM)
    set(IOS_PLATFORM "OS")  # Default to device (arm64)
endif()

# Map platform to architecture
if(IOS_PLATFORM STREQUAL "OS")
    set(IOS_ARCH arm64)
elseif(IOS_PLATFORM STREQUAL "OS32")
    set(IOS_ARCH armv7)
elseif(IOS_PLATFORM STREQUAL "SIMULATOR")
    set(IOS_ARCH i386)
elseif(IOS_PLATFORM STREQUAL "SIMULATOR64")
    set(IOS_ARCH x86_64)
else()
    message(FATAL_ERROR "Unknown IOS_PLATFORM: ${IOS_PLATFORM}. Must be OS, OS32, SIMULATOR, or SIMULATOR64")
endif()

# Set architecture and processors
set(CMAKE_OSX_ARCHITECTURES ${IOS_ARCH})
set(CMAKE_SYSTEM_PROCESSOR ${IOS_ARCH})

# For GNUInstallDirs compatibility - define the target architecture
if(IOS_ARCH STREQUAL "arm64")
    set(_IOS_INSTALL_ARCH "arm64")
elseif(IOS_ARCH STREQUAL "armv7")
    set(_IOS_INSTALL_ARCH "armv7")
elseif(IOS_ARCH STREQUAL "x86_64")
    set(_IOS_INSTALL_ARCH "x86_64")
elseif(IOS_ARCH STREQUAL "i386")
    set(_IOS_INSTALL_ARCH "i386")
endif()

# Set CMAKE_LIBRARY_ARCHITECTURE for GNUInstallDirs
set(CMAKE_LIBRARY_ARCHITECTURE "${_IOS_INSTALL_ARCH}-apple-ios")

# Get actual iOS SDK path using xcrun
if(IOS_PLATFORM STREQUAL "SIMULATOR" OR IOS_PLATFORM STREQUAL "SIMULATOR64")
    set(IOS_SDK_NAME iphonesimulator)
else()
    set(IOS_SDK_NAME iphoneos)
endif()

# Execute xcrun to get SDK path if not already set
if(NOT CMAKE_OSX_SYSROOT)
    execute_process(
        COMMAND xcrun --sdk ${IOS_SDK_NAME} --show-sdk-path
        OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

message(STATUS "iOS Toolchain: Platform=${IOS_PLATFORM}, Arch=${IOS_ARCH}, SDK=${CMAKE_OSX_SYSROOT}")

# Set deployment target
set(CMAKE_OSX_DEPLOYMENT_TARGET ${IOS_DEPLOYMENT_TARGET})

# Set compiler
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# Compiler flags for iOS cross-compilation
set(CMAKE_C_FLAGS "-fPIC -isysroot ${CMAKE_OSX_SYSROOT} -arch ${IOS_ARCH} -miphoneos-version-min=${IOS_DEPLOYMENT_TARGET}")
set(CMAKE_CXX_FLAGS "-fPIC -isysroot ${CMAKE_OSX_SYSROOT} -arch ${IOS_ARCH} -miphoneos-version-min=${IOS_DEPLOYMENT_TARGET}")

# Linker flags
set(CMAKE_EXE_LINKER_FLAGS "-isysroot ${CMAKE_OSX_SYSROOT} -arch ${IOS_ARCH}")
set(CMAKE_SHARED_LINKER_FLAGS "-isysroot ${CMAKE_OSX_SYSROOT} -arch ${IOS_ARCH}")
set(CMAKE_MODULE_LINKER_FLAGS "-isysroot ${CMAKE_OSX_SYSROOT} -arch ${IOS_ARCH}")

# Tell CMake that we're cross-compiling
set(CMAKE_CROSSCOMPILING ON)

# Disable some checks that don't work well with cross-compilation
set(CMAKE_C_ABI_COMPILED TRUE)
set(CMAKE_CXX_ABI_COMPILED TRUE)

# Search paths - only look in iOS SDK
set(CMAKE_FIND_ROOT_PATH ${CMAKE_OSX_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Disable any platform-specific checks that might not work on iOS
set(CMAKE_SKIP_RPATH ON)

# =============================================================================
# Auto-detect and configure OpenSSL iOS if available in deps cache
# =============================================================================
# This allows libdatachannel and other dependencies to find OpenSSL headers
# when building for iOS in the main project's deps cache

if(DEFINED IOS_DEPS_CACHE_DIR)
    set(_OPENSSL_ROOT_DIR "${IOS_DEPS_CACHE_DIR}/openssl")
    set(_OPENSSL_INCLUDE_DIR "${_OPENSSL_ROOT_DIR}/include")
    set(_OPENSSL_LIB_DIR "${_OPENSSL_ROOT_DIR}/lib")

    if(EXISTS "${_OPENSSL_LIB_DIR}/libssl.a" AND EXISTS "${_OPENSSL_INCLUDE_DIR}/openssl/ssl.h")
        message(STATUS "iOS Toolchain: Auto-detected OpenSSL at ${_OPENSSL_ROOT_DIR}")

        # Set OpenSSL variables so find_package(OpenSSL) succeeds
        set(OPENSSL_ROOT_DIR "${_OPENSSL_ROOT_DIR}" CACHE PATH "OpenSSL root" FORCE)
        set(OPENSSL_INCLUDE_DIR "${_OPENSSL_INCLUDE_DIR}" CACHE PATH "OpenSSL include" FORCE)
        set(OPENSSL_CRYPTO_LIBRARY "${_OPENSSL_LIB_DIR}/libcrypto.a" CACHE FILEPATH "OpenSSL crypto" FORCE)
        set(OPENSSL_SSL_LIBRARY "${_OPENSSL_LIB_DIR}/libssl.a" CACHE FILEPATH "OpenSSL SSL" FORCE)
        set(OPENSSL_FOUND TRUE CACHE BOOL "OpenSSL found" FORCE)

        # Add OpenSSL include directory to default include search paths
        # This ensures C++ headers like <openssl/ssl.h> are found
        list(APPEND CMAKE_CXX_FLAGS "-I${_OPENSSL_INCLUDE_DIR}")
        list(APPEND CMAKE_C_FLAGS "-I${_OPENSSL_INCLUDE_DIR}")

        # Also add to CMAKE_PREFIX_PATH for find_package(OpenSSL)
        list(APPEND CMAKE_PREFIX_PATH "${_OPENSSL_ROOT_DIR}")
    endif()
endif()
