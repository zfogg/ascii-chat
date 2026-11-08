#
# OpenSSL dependency configuration
# Provides: OpenSSL::Crypto, OpenSSL::SSL (or static equivalents)
#

set(ASCIICHAT_OPENSSL_RUNTIME_DIR "" CACHE INTERNAL "Runtime directory for selected shared OpenSSL" FORCE)
set(ASCIICHAT_OPENSSL_SOURCE_VERSION "3.6.0")
set(ASCIICHAT_OPENSSL_SOURCE_SHA256 "b6a5f44b7eb69e3fa35dbf15524405b44837a481d43d81daddde3ff21fcbb8e9")
set(ASCIICHAT_OPENSSL_BUILD_JOBS 4 CACHE STRING "Parallel jobs for source-built OpenSSL")
if(NOT ASCIICHAT_OPENSSL_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "ASCIICHAT_OPENSSL_BUILD_JOBS must be a positive integer")
endif()
# Handle iOS builds - OpenSSL is built from source at configure time
if(PLATFORM_IOS)
    message(STATUS "Configuring ${BoldBlue}OpenSSL${ColorReset} from source (iOS cross-compile)...")

    set(OPENSSL_VERSION "${ASCIICHAT_OPENSSL_SOURCE_VERSION}")
    if(BUILD_IOS_SIM)
        set(_OPENSSL_IOS_VARIANT "sim")
        set(_OPENSSL_IOS_MIN_VERSION_FLAG "-mios-simulator-version-min=16.0")
    else()
        set(_OPENSSL_IOS_VARIANT "device")
        set(_OPENSSL_IOS_MIN_VERSION_FLAG "-miphoneos-version-min=16.0")
    endif()
    set(OPENSSL_LIBDIR "lib")

    if(BUILD_IOS_SIM)
        set(_OPENSSL_IOS_ARCH "${CMAKE_OSX_ARCHITECTURES}")
        if(_OPENSSL_IOS_ARCH STREQUAL "arm64")
            set(_OPENSSL_IOS_CONFIG "darwin64-arm64-cc")
        else()
            set(_OPENSSL_IOS_CONFIG "darwin64-x86_64-cc")
        endif()
    else()
        set(_OPENSSL_IOS_ARCH "arm64")
        set(_OPENSSL_IOS_CONFIG "darwin64-arm64-cc")
    endif()
    string(REPLACE ";" "-" _OPENSSL_IOS_ARCH_KEY "${_OPENSSL_IOS_ARCH}")
    set(OPENSSL_PREFIX "${IOS_DEPS_CACHE_DIR}/openssl-${OPENSSL_VERSION}-${_OPENSSL_IOS_VARIANT}-${_OPENSSL_IOS_ARCH_KEY}")
    set(OPENSSL_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/openssl-ios-${OPENSSL_VERSION}-${_OPENSSL_IOS_VARIANT}-${_OPENSSL_IOS_ARCH_KEY}-build")
    set(OPENSSL_SOURCE_DIR "${OPENSSL_BUILD_DIR}/src/openssl")

    # Get actual iOS SDK path using xcrun
    if(BUILD_IOS_SIM)
        execute_process(COMMAND xcrun --sdk iphonesimulator --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
    else()
        execute_process(COMMAND xcrun --sdk iphoneos --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()

    message(STATUS "  iOS SDK path: ${IOS_SDK_PATH}")

    # Reject a cached archive built for a different iOS architecture. This can
    # happen when a simulator and device build share the dependency cache.
    set(_OPENSSL_CACHE_VALID FALSE)
    if(EXISTS "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl.a" AND EXISTS "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libcrypto.a")
        execute_process(COMMAND lipo -info "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl.a"
                        OUTPUT_VARIABLE _OPENSSL_ARCH_INFO ERROR_VARIABLE _OPENSSL_ARCH_ERROR
                        RESULT_VARIABLE _OPENSSL_LIPO_RESULT)
        if(NOT _OPENSSL_LIPO_RESULT EQUAL 0)
            message(FATAL_ERROR "Could not inspect cached OpenSSL archive: ${_OPENSSL_ARCH_ERROR}")
        elseif(_OPENSSL_ARCH_INFO MATCHES "${_OPENSSL_IOS_ARCH}")
            set(_OPENSSL_CACHE_VALID TRUE)
        else()
            message(STATUS "  OpenSSL cache architecture mismatch (expected ${_OPENSSL_IOS_ARCH}); rebuilding")
            file(REMOVE_RECURSE "${OPENSSL_PREFIX}")
        endif()
    endif()

    # Build OpenSSL synchronously at configure time if not cached
    if(NOT _OPENSSL_CACHE_VALID)
        message(STATUS "  OpenSSL library not found in cache, will build from source...")

        file(MAKE_DIRECTORY "${OPENSSL_BUILD_DIR}")
        file(MAKE_DIRECTORY "${OPENSSL_SOURCE_DIR}")

        set(OPENSSL_TARBALL "${OPENSSL_BUILD_DIR}/openssl-${OPENSSL_VERSION}.tar.gz")
        if(NOT EXISTS "${OPENSSL_TARBALL}")
            message(STATUS "  Downloading OpenSSL ${OPENSSL_VERSION}...")
            file(DOWNLOAD
                "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz"
                "${OPENSSL_TARBALL}"
                EXPECTED_HASH "SHA256=${ASCIICHAT_OPENSSL_SOURCE_SHA256}"
                STATUS DOWNLOAD_STATUS
                SHOW_PROGRESS
            )
            list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
            if(NOT STATUS_CODE EQUAL 0)
                list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
                message(FATAL_ERROR "Failed to download OpenSSL: ${ERROR_MSG}")
            endif()
        endif()

        # Extract tarball
        if(NOT EXISTS "${OPENSSL_SOURCE_DIR}/Configure")
            message(STATUS "  Extracting OpenSSL...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf "${OPENSSL_TARBALL}"
                WORKING_DIRECTORY "${OPENSSL_BUILD_DIR}"
                RESULT_VARIABLE EXTRACT_RESULT
            )
            if(NOT EXTRACT_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to extract OpenSSL tarball")
            endif()
            file(RENAME "${OPENSSL_BUILD_DIR}/openssl-${OPENSSL_VERSION}" "${OPENSSL_SOURCE_DIR}")
        endif()

        # Configure OpenSSL for iOS using clang directly (more reliable than ios64-cross)
        message(STATUS "  Configuring OpenSSL for iOS (${_OPENSSL_IOS_ARCH})...")

        execute_process(
            COMMAND bash -c "
                export CC='${CMAKE_C_COMPILER}'
                export CFLAGS='-arch ${_OPENSSL_IOS_ARCH} -isysroot ${IOS_SDK_PATH} ${_OPENSSL_IOS_MIN_VERSION_FLAG} -fPIC'
                export LDFLAGS='-arch ${_OPENSSL_IOS_ARCH} -isysroot ${IOS_SDK_PATH} -fPIC'
                '${OPENSSL_SOURCE_DIR}/Configure' ${_OPENSSL_IOS_CONFIG} --prefix=${OPENSSL_PREFIX} --libdir=lib no-shared no-tests no-apps < /dev/null
            "
            WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
            TIMEOUT 60
            RESULT_VARIABLE CONFIG_RESULT
            OUTPUT_VARIABLE CONFIG_OUTPUT
            ERROR_VARIABLE CONFIG_ERROR
        )
        message(STATUS "  Configure result: ${CONFIG_RESULT}")
        if(CONFIG_OUTPUT)
            message(STATUS "  Configure stdout:\n${CONFIG_OUTPUT}")
        endif()
        if(CONFIG_ERROR)
            message(STATUS "  Configure stderr:\n${CONFIG_ERROR}")
        endif()
        if(NOT CONFIG_RESULT EQUAL 0 AND NOT CONFIG_RESULT EQUAL -1)
            message(FATAL_ERROR "Failed to configure OpenSSL for iOS (result: ${CONFIG_RESULT})")
        elseif(CONFIG_RESULT EQUAL -1)
            message(FATAL_ERROR "OpenSSL configure timed out after 60 seconds")
        endif()

        message(STATUS "  Building OpenSSL (this takes a few minutes)...")
        execute_process(
            COMMAND make -j${ASCIICHAT_OPENSSL_BUILD_JOBS} build_libs
            WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
            RESULT_VARIABLE BUILD_RESULT
            OUTPUT_VARIABLE BUILD_OUTPUT
            ERROR_VARIABLE BUILD_ERROR
        )
        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build OpenSSL:\n${BUILD_ERROR}\n${BUILD_OUTPUT}")
        endif()

        message(STATUS "  Installing OpenSSL...")
        execute_process(
            COMMAND make install_sw
            WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
            RESULT_VARIABLE INSTALL_RESULT
            OUTPUT_VARIABLE INSTALL_OUTPUT
            ERROR_VARIABLE INSTALL_ERROR
        )
        if(NOT INSTALL_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to install OpenSSL:\n${INSTALL_ERROR}\n${INSTALL_OUTPUT}")
        endif()

        message(STATUS "  ${BoldBlue}OpenSSL${ColorReset} built and installed successfully")
    else()
        message(STATUS "  ${BoldBlue}OpenSSL${ColorReset} library found in cache: ${BoldMagenta}${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl.a${ColorReset}")
    endif()

    # Create CMake targets
    add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
    add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
    set_target_properties(OpenSSL::Crypto PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libcrypto.a"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_PREFIX}/include"
    )
    set_target_properties(OpenSSL::SSL PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl.a"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_PREFIX}/include"
        INTERFACE_LINK_LIBRARIES OpenSSL::Crypto
    )

    set(OPENSSL_FOUND TRUE)
    set(OPENSSL_ROOT_DIR "${OPENSSL_PREFIX}" CACHE PATH "OpenSSL root directory" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libcrypto.a" CACHE FILEPATH "OpenSSL Crypto library" FORCE)
    set(OPENSSL_SSL_LIBRARY "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl.a" CACHE FILEPATH "OpenSSL SSL library" FORCE)
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_PREFIX}/include")

    return()
endif()

# Handle musl builds - OpenSSL is built from source at configure time
if(USE_MUSL)
    # OpenSSL MUST be built synchronously at configure time because libdatachannel's
    # configure also runs at configure time (via execute_process in Libdatachannel.cmake).
    # If we use ExternalProject (which runs at build time), OpenSSL won't exist when
    # libdatachannel tries to find_package(OpenSSL).
    message(STATUS "Configuring ${BoldBlue}OpenSSL${ColorReset} from source...")

    set(OPENSSL_VERSION "${ASCIICHAT_OPENSSL_SOURCE_VERSION}")
    set(OPENSSL_PREFIX "${MUSL_DEPS_DIR_STATIC}/openssl-${OPENSSL_VERSION}")
    set(OPENSSL_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/openssl-${OPENSSL_VERSION}-build")
    set(OPENSSL_SOURCE_DIR "${OPENSSL_BUILD_DIR}/src/openssl")

    # Detect target architecture for OpenSSL Configure
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(OPENSSL_TARGET "linux-aarch64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
        set(OPENSSL_TARGET "linux-x86_64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i386|i686")
        set(OPENSSL_TARGET "linux-x86")
    else()
        set(OPENSSL_TARGET "linux-generic64")
    endif()
    set(OPENSSL_LIBDIR "lib")
    set(OPENSSL_CONFIG_SIGNATURE "target=${OPENSSL_TARGET};version=${OPENSSL_VERSION};shared=OFF;zlib=OFF;cflags=${MUSL_KERNEL_CFLAGS}")
    set(OPENSSL_CONFIG_STAMP "${OPENSSL_BUILD_DIR}/.config-signature")

    # The musl release build does not provide zlib. Keep OpenSSL's optional
    # compression support disabled so libdatachannel's static OpenSSL target
    # does not acquire an unavailable ZLIB::ZLIB dependency. Rebuild caches
    # made with the previous configuration before accepting their archives.
    set(_OPENSSL_RECONFIGURE_REQUIRED TRUE)
    if(EXISTS "${OPENSSL_CONFIG_STAMP}")
        file(READ "${OPENSSL_CONFIG_STAMP}" _OPENSSL_CACHED_SIGNATURE)
        string(STRIP "${_OPENSSL_CACHED_SIGNATURE}" _OPENSSL_CACHED_SIGNATURE)
        if(_OPENSSL_CACHED_SIGNATURE STREQUAL "${OPENSSL_CONFIG_SIGNATURE}")
            set(_OPENSSL_RECONFIGURE_REQUIRED FALSE)
        endif()
    endif()
    if(_OPENSSL_RECONFIGURE_REQUIRED)
        file(REMOVE_RECURSE "${OPENSSL_SOURCE_DIR}" "${OPENSSL_PREFIX}")
    endif()

    # Build OpenSSL synchronously at configure time if not cached
    if(NOT EXISTS "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl.a" OR NOT EXISTS "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libcrypto.a")
        message(STATUS "  OpenSSL library not found in cache, will build from source")
        message(STATUS "  This may take a few minutes on first build...")

        file(MAKE_DIRECTORY "${OPENSSL_BUILD_DIR}")
        file(MAKE_DIRECTORY "${OPENSSL_SOURCE_DIR}")

        set(OPENSSL_TARBALL "${OPENSSL_BUILD_DIR}/openssl-${OPENSSL_VERSION}.tar.gz")
        if(NOT EXISTS "${OPENSSL_TARBALL}")
            message(STATUS "  Downloading OpenSSL ${OPENSSL_VERSION}...")
            file(DOWNLOAD
                "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz"
                "${OPENSSL_TARBALL}"
                EXPECTED_HASH "SHA256=${ASCIICHAT_OPENSSL_SOURCE_SHA256}"
                STATUS DOWNLOAD_STATUS
                SHOW_PROGRESS
            )
            list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
            if(NOT STATUS_CODE EQUAL 0)
                list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
                message(FATAL_ERROR "Failed to download OpenSSL: ${ERROR_MSG}")
            endif()
        endif()

        # Extract tarball
        if(NOT EXISTS "${OPENSSL_SOURCE_DIR}/Configure")
            message(STATUS "  Extracting OpenSSL...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf "${OPENSSL_TARBALL}"
                WORKING_DIRECTORY "${OPENSSL_BUILD_DIR}"
                RESULT_VARIABLE EXTRACT_RESULT
            )
            if(NOT EXTRACT_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to extract OpenSSL tarball")
            endif()
            # Move from openssl-X.Y.Z/ to src/openssl/
            file(RENAME "${OPENSSL_BUILD_DIR}/openssl-${OPENSSL_VERSION}" "${OPENSSL_SOURCE_DIR}")
        endif()

        # Configure OpenSSL
        message(STATUS "  Configuring OpenSSL for ${OPENSSL_TARGET}...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env
                CC=${MUSL_GCC}
                REALGCC=${REAL_GCC}
                CFLAGS=${MUSL_KERNEL_CFLAGS}
                "${OPENSSL_SOURCE_DIR}/Configure"
                ${OPENSSL_TARGET}
                --prefix=${OPENSSL_PREFIX}
                --libdir=lib
                no-shared
                no-zlib
                no-tests
                no-ui-console
                -fPIC
            WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
            RESULT_VARIABLE CONFIG_RESULT
            OUTPUT_VARIABLE CONFIG_OUTPUT
            ERROR_VARIABLE CONFIG_ERROR
        )
        if(NOT CONFIG_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to configure OpenSSL:\n${CONFIG_ERROR}")
        endif()

        message(STATUS "  Building OpenSSL (this takes a few minutes)...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env REALGCC=${REAL_GCC} make -j${ASCIICHAT_OPENSSL_BUILD_JOBS}
            WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
            RESULT_VARIABLE BUILD_RESULT
            OUTPUT_VARIABLE BUILD_OUTPUT
            ERROR_VARIABLE BUILD_ERROR
        )
        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build OpenSSL:\n${BUILD_ERROR}")
        endif()

        # Install OpenSSL
        message(STATUS "  Installing OpenSSL...")
        execute_process(
            COMMAND make install_sw
            WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
            RESULT_VARIABLE INSTALL_RESULT
            OUTPUT_VARIABLE INSTALL_OUTPUT
            ERROR_VARIABLE INSTALL_ERROR
        )
        if(NOT INSTALL_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to install OpenSSL:\n${INSTALL_ERROR}")
        endif()

        file(WRITE "${OPENSSL_CONFIG_STAMP}" "${OPENSSL_CONFIG_SIGNATURE}\n")

        message(STATUS "  ${BoldGreen}OpenSSL${ColorReset} built and cached successfully")
    else()
        message(STATUS "  ${BoldBlue}OpenSSL${ColorReset} library found in cache: ${BoldMagenta}${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl.a${ColorReset}")
    endif()

    # Set OpenSSL variables for CMake find_package to use
    set(OPENSSL_ROOT_DIR "${OPENSSL_PREFIX}" CACHE PATH "OpenSSL root directory" FORCE)
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_PREFIX}/include" CACHE PATH "OpenSSL include directory" FORCE)
    set(OPENSSL_SSL_LIBRARY "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl.a" CACHE FILEPATH "OpenSSL SSL library" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libcrypto.a" CACHE FILEPATH "OpenSSL Crypto library" FORCE)

    # Create imported targets that match what find_package(OpenSSL) would create
    if(NOT TARGET OpenSSL::Crypto)
        add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
        set_target_properties(OpenSSL::Crypto PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libcrypto.a"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_PREFIX}/include"
        )
    endif()

    if(NOT TARGET OpenSSL::SSL)
        add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
        set_target_properties(OpenSSL::SSL PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl.a"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_PREFIX}/include"
            INTERFACE_LINK_LIBRARIES OpenSSL::Crypto
        )
    endif()

    # Create a custom target for musl builds (libraries are pre-built/cached)
    add_custom_target(openssl-musl)

    return()
endif()

# Skip if already configured by MuslDependencies.cmake
if(TARGET OpenSSL::Crypto)
    message(STATUS "  ${BoldBlue}OpenSSL${ColorReset} already configured")
    return()
endif()

# =============================================================================
# Native Linux/macOS: Build the required QUIC-capable OpenSSL, including shared-dependency builds
# Source builds use a versioned cache so upgrading does not replace existing caches.
# =============================================================================
if(NOT USE_MUSL AND NOT WIN32 AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev" OR CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
    set(OPENSSL_VERSION "${ASCIICHAT_OPENSSL_SOURCE_VERSION}")
    set(OPENSSL_DOWNLOAD_TAG "openssl-${OPENSSL_VERSION}")
    set(OPENSSL_EXPECTED_HASH "SHA256=${ASCIICHAT_OPENSSL_SOURCE_SHA256}")
    message(STATUS "Configuring ${BoldBlue}OpenSSL ${OPENSSL_VERSION}${ColorReset} from source...")

    set(OPENSSL_PREFIX "${ASCIICHAT_DEPS_CACHE_DIR}/openssl-${OPENSSL_VERSION}")
    set(OPENSSL_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/openssl-${OPENSSL_VERSION}-build")
    set(OPENSSL_SOURCE_DIR "${OPENSSL_BUILD_DIR}/src/openssl")
    set(OPENSSL_TARGET_STAMP "${OPENSSL_BUILD_DIR}/.target")
    set(OPENSSL_NO_ASM OFF)
    # Release packages should not retain a runtime dependency on the build
    # machine's OpenSSL dylib or its install path. Debug/dev builds keep shared
    # OpenSSL for faster iteration.
    if(NOT ASCIICHAT_SHARED_DEPS AND (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
        set(OPENSSL_BUILD_SHARED OFF)
    else()
        set(OPENSSL_BUILD_SHARED ON)
    endif()

    set(OPENSSL_CONFIGURE_PREFIX "${OPENSSL_PREFIX}")
    set(OPENSSL_INSTALL_ARGS)
    if(NOT OPENSSL_BUILD_SHARED)
        # Runtime defaults must not name a disposable build checkout. Stage the
        # conventional prefix under the dependency cache using DESTDIR.
        set(OPENSSL_CONFIGURE_PREFIX "/usr/local")
        set(OPENSSL_STAGING_ROOT "${OPENSSL_PREFIX}")
        set(OPENSSL_PREFIX "${OPENSSL_STAGING_ROOT}/usr/local")
        list(APPEND OPENSSL_INSTALL_ARGS "DESTDIR=${OPENSSL_STAGING_ROOT}")
    endif()

    # Detect target architecture for OpenSSL Configure.
    # Important: Check APPLE first so arm64 Macs don't get classified as linux-aarch64.
    if(APPLE)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            set(OPENSSL_TARGET "darwin64-arm64-cc")
        else()
            set(OPENSSL_TARGET "darwin64-x86_64-cc")
        endif()
        set(OPENSSL_NO_ASM ON)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(OPENSSL_TARGET "linux-aarch64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
        set(OPENSSL_TARGET "linux-x86_64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i386|i686")
        set(OPENSSL_TARGET "linux-x86")
    else()
        set(OPENSSL_TARGET "linux-generic64")
    endif()
    set(OPENSSL_LIBDIR "lib")

    set(OPENSSL_CONFIG_SIGNATURE "target=${OPENSSL_TARGET};no_asm=${OPENSSL_NO_ASM};shared=${OPENSSL_BUILD_SHARED};version=${OPENSSL_VERSION};prefix=${OPENSSL_CONFIGURE_PREFIX};cc=${CMAKE_C_COMPILER};cc_version=${CMAKE_C_COMPILER_VERSION}")

    if(OPENSSL_BUILD_SHARED)
        set(_OPENSSL_EXPECTED_SSL_LIB "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(_OPENSSL_EXPECTED_CRYPTO_LIB "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libcrypto${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(_OPENSSL_IMPORTED_TYPE SHARED)
    else()
        set(_OPENSSL_EXPECTED_SSL_LIB "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl.a")
        set(_OPENSSL_EXPECTED_CRYPTO_LIB "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libcrypto.a")
        set(_OPENSSL_IMPORTED_TYPE STATIC)
    endif()

    # If cached OpenSSL was generated for a different target, wipe and re-extract.
    set(_OPENSSL_RECONFIGURE_REQUIRED FALSE)
    if(EXISTS "${OPENSSL_TARGET_STAMP}")
        file(READ "${OPENSSL_TARGET_STAMP}" _OPENSSL_CACHED_SIGNATURE)
        string(STRIP "${_OPENSSL_CACHED_SIGNATURE}" _OPENSSL_CACHED_SIGNATURE)
        if(NOT _OPENSSL_CACHED_SIGNATURE STREQUAL "${OPENSSL_CONFIG_SIGNATURE}")
            set(_OPENSSL_RECONFIGURE_REQUIRED TRUE)
            message(STATUS "  OpenSSL configuration changed: ${_OPENSSL_CACHED_SIGNATURE} -> ${OPENSSL_CONFIG_SIGNATURE}; forcing clean reconfigure")
        endif()
    elseif(EXISTS "${OPENSSL_SOURCE_DIR}/configdata.pm")
        # Legacy cache from before signature stamping; force a clean rebuild.
        set(_OPENSSL_RECONFIGURE_REQUIRED TRUE)
        message(STATUS "  OpenSSL cache missing configuration signature; forcing clean reconfigure")
    endif()

    if(_OPENSSL_RECONFIGURE_REQUIRED)
        file(REMOVE_RECURSE "${OPENSSL_SOURCE_DIR}")
        file(REMOVE_RECURSE "${OPENSSL_PREFIX}")
    endif()

    # Build OpenSSL if not cached
    if(NOT EXISTS "${_OPENSSL_EXPECTED_SSL_LIB}" OR NOT EXISTS "${_OPENSSL_EXPECTED_CRYPTO_LIB}")
        message(STATUS "  OpenSSL ${OPENSSL_VERSION} not found in cache, building from source...")

        file(MAKE_DIRECTORY "${OPENSSL_BUILD_DIR}")
        file(MAKE_DIRECTORY "${OPENSSL_SOURCE_DIR}")

        # Download OpenSSL source
        set(OPENSSL_TARBALL "${OPENSSL_BUILD_DIR}/openssl-${OPENSSL_VERSION}.tar.gz")
        if(NOT EXISTS "${OPENSSL_TARBALL}")
            message(STATUS "  Downloading OpenSSL ${OPENSSL_VERSION}...")
            file(DOWNLOAD
                "https://github.com/openssl/openssl/releases/download/${OPENSSL_DOWNLOAD_TAG}/openssl-${OPENSSL_VERSION}.tar.gz"
                "${OPENSSL_TARBALL}"
                EXPECTED_HASH "${OPENSSL_EXPECTED_HASH}"
                SHOW_PROGRESS
            )
        endif()

        # Extract tarball
        if(NOT EXISTS "${OPENSSL_SOURCE_DIR}/Configure")
            message(STATUS "  Extracting OpenSSL...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf "${OPENSSL_TARBALL}"
                WORKING_DIRECTORY "${OPENSSL_BUILD_DIR}"
                RESULT_VARIABLE EXTRACT_RESULT
            )
            if(NOT EXTRACT_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to extract OpenSSL tarball")
            endif()
            file(RENAME "${OPENSSL_BUILD_DIR}/openssl-${OPENSSL_VERSION}" "${OPENSSL_SOURCE_DIR}")
        endif()

        # Configure OpenSSL
        set(OPENSSL_CONFIGURE_ARGS
            ${OPENSSL_TARGET}
            --prefix=${OPENSSL_CONFIGURE_PREFIX}
            --openssldir=/etc/ssl
            --libdir=lib
            no-tests
            -fPIC
        )
        if(OPENSSL_NO_ASM)
            list(APPEND OPENSSL_CONFIGURE_ARGS no-asm)
        endif()
        if(OPENSSL_BUILD_SHARED)
            list(APPEND OPENSSL_CONFIGURE_ARGS shared)
        else()
            list(APPEND OPENSSL_CONFIGURE_ARGS no-shared)
        endif()
        message(STATUS "  Configuring OpenSSL for ${OPENSSL_TARGET}...")
        # Keep dependency compilation consistent with the selected project toolchain.
        set(_OPENSSL_ENV_CMD ${CMAKE_COMMAND} -E env "CC=${CMAKE_C_COMPILER}")
        execute_process(
            COMMAND ${_OPENSSL_ENV_CMD}
                "${OPENSSL_SOURCE_DIR}/Configure"
                ${OPENSSL_CONFIGURE_ARGS}
            WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
            RESULT_VARIABLE CONFIG_RESULT
            OUTPUT_VARIABLE CONFIG_OUTPUT
            ERROR_VARIABLE CONFIG_ERROR
        )
        if(NOT CONFIG_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to configure OpenSSL:\n${CONFIG_ERROR}")
        endif()

        message(STATUS "  Building OpenSSL (this takes a few minutes)...")
        execute_process(
            COMMAND ${_OPENSSL_ENV_CMD} make -j${ASCIICHAT_OPENSSL_BUILD_JOBS}
            WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
            RESULT_VARIABLE BUILD_RESULT
        )
        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build OpenSSL")
        endif()

        # Install OpenSSL
        message(STATUS "  Installing OpenSSL...")
        execute_process(
            COMMAND ${_OPENSSL_ENV_CMD} make install_sw ${OPENSSL_INSTALL_ARGS}
            WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
            RESULT_VARIABLE INSTALL_RESULT
        )
        if(NOT INSTALL_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to install OpenSSL")
        endif()

        file(WRITE "${OPENSSL_TARGET_STAMP}" "${OPENSSL_CONFIG_SIGNATURE}\n")

        message(STATUS "  ${BoldGreen}OpenSSL ${OPENSSL_VERSION}${ColorReset} built and cached successfully")
    else()
        message(STATUS "  ${BoldBlue}OpenSSL ${OPENSSL_VERSION}${ColorReset} found in cache: ${BoldMagenta}${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}${ColorReset}")
    endif()

    # Resolve installed OpenSSL library paths (handle versioned shared-library names).
    set(OPENSSL_SSL_LIBRARY "${_OPENSSL_EXPECTED_SSL_LIB}")
    set(OPENSSL_CRYPTO_LIBRARY "${_OPENSSL_EXPECTED_CRYPTO_LIB}")
    if(OPENSSL_BUILD_SHARED)
        if(NOT EXISTS "${OPENSSL_SSL_LIBRARY}")
            file(GLOB _OPENSSL_SSL_CANDIDATES "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl*${CMAKE_SHARED_LIBRARY_SUFFIX}")
            list(SORT _OPENSSL_SSL_CANDIDATES)
            list(LENGTH _OPENSSL_SSL_CANDIDATES _OPENSSL_SSL_CANDIDATE_COUNT)
            if(_OPENSSL_SSL_CANDIDATE_COUNT GREATER 0)
                list(GET _OPENSSL_SSL_CANDIDATES 0 OPENSSL_SSL_LIBRARY)
            endif()
        endif()
        if(NOT EXISTS "${OPENSSL_CRYPTO_LIBRARY}")
            file(GLOB _OPENSSL_CRYPTO_CANDIDATES "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libcrypto*${CMAKE_SHARED_LIBRARY_SUFFIX}")
            list(SORT _OPENSSL_CRYPTO_CANDIDATES)
            list(LENGTH _OPENSSL_CRYPTO_CANDIDATES _OPENSSL_CRYPTO_CANDIDATE_COUNT)
            if(_OPENSSL_CRYPTO_CANDIDATE_COUNT GREATER 0)
                list(GET _OPENSSL_CRYPTO_CANDIDATES 0 OPENSSL_CRYPTO_LIBRARY)
            endif()
        endif()
    endif()
    if(NOT EXISTS "${OPENSSL_SSL_LIBRARY}" OR NOT EXISTS "${OPENSSL_CRYPTO_LIBRARY}")
        message(FATAL_ERROR "OpenSSL build succeeded but libraries were not found in ${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}")
    endif()

    if(OPENSSL_BUILD_SHARED)
        set(ASCIICHAT_OPENSSL_RUNTIME_DIR "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}"
            CACHE INTERNAL "Runtime directory for selected shared OpenSSL" FORCE)
        list(PREPEND CMAKE_BUILD_RPATH "${ASCIICHAT_OPENSSL_RUNTIME_DIR}")
        list(REMOVE_DUPLICATES CMAKE_BUILD_RPATH)
    endif()

    set(OPENSSL_ROOT_DIR "${OPENSSL_PREFIX}" CACHE PATH "OpenSSL root directory" FORCE)
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_PREFIX}/include" CACHE PATH "OpenSSL include directory" FORCE)
    set(OPENSSL_SSL_LIBRARY "${OPENSSL_SSL_LIBRARY}" CACHE FILEPATH "OpenSSL SSL library" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_CRYPTO_LIBRARY}" CACHE FILEPATH "OpenSSL Crypto library" FORCE)

    # Create imported targets for OpenSSL ${OPENSSL_VERSION}
    if(NOT TARGET OpenSSL::Crypto)
        add_library(OpenSSL::Crypto ${_OPENSSL_IMPORTED_TYPE} IMPORTED GLOBAL)
        set_target_properties(OpenSSL::Crypto PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_CRYPTO_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_PREFIX}/include"
        )
    endif()

    if(NOT TARGET OpenSSL::SSL)
        add_library(OpenSSL::SSL ${_OPENSSL_IMPORTED_TYPE} IMPORTED GLOBAL)
        set_target_properties(OpenSSL::SSL PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_SSL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_PREFIX}/include"
            INTERFACE_LINK_LIBRARIES OpenSSL::Crypto
        )
    endif()

    set(OpenSSL_FOUND TRUE)
    message(STATUS "  ${BoldGreen}✓${ColorReset} OpenSSL ${OPENSSL_VERSION} configured for native build")
    return()
endif()

# =============================================================================
# macOS Release: Use the versioned source-build cache when available
# =============================================================================
set(_OPENSSL_STATIC_FOUND FALSE)
if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT ASCIICHAT_SHARED_DEPS)
    set(_OPENSSL_HOMEBREW_PATH "")
    set(OPENSSL_PREFIX "${ASCIICHAT_DEPS_CACHE_DIR}/openssl-${ASCIICHAT_OPENSSL_SOURCE_VERSION}/usr/local")
    set(OPENSSL_LIBDIR "lib")

    # Check if the cached static libraries exist.
    if(EXISTS "${OPENSSL_PREFIX}/lib/libssl.a" AND EXISTS "${OPENSSL_PREFIX}/lib/libcrypto.a")
        set(_OPENSSL_HOMEBREW_PATH "${OPENSSL_PREFIX}")
    endif()

    if(_OPENSSL_HOMEBREW_PATH)
        find_library(OPENSSL_CRYPTO_STATIC NAMES libcrypto.a
            PATHS "${_OPENSSL_HOMEBREW_PATH}/lib" NO_DEFAULT_PATH)
        find_library(OPENSSL_SSL_STATIC NAMES libssl.a
            PATHS "${_OPENSSL_HOMEBREW_PATH}/lib" NO_DEFAULT_PATH)
        find_path(OPENSSL_INCLUDE_DIR NAMES openssl/ssl.h
            PATHS "${_OPENSSL_HOMEBREW_PATH}/include" NO_DEFAULT_PATH)

        if(OPENSSL_CRYPTO_STATIC AND OPENSSL_SSL_STATIC AND OPENSSL_INCLUDE_DIR)
            set(_OPENSSL_STATIC_FOUND TRUE)

            # Create imported targets for static OpenSSL
            add_library(OpenSSL::Crypto STATIC IMPORTED)
            set_target_properties(OpenSSL::Crypto PROPERTIES
                IMPORTED_LOCATION "${OPENSSL_CRYPTO_STATIC}"
                INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
            )

            add_library(OpenSSL::SSL STATIC IMPORTED)
            set_target_properties(OpenSSL::SSL PROPERTIES
                IMPORTED_LOCATION "${OPENSSL_SSL_STATIC}"
                INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
                INTERFACE_LINK_LIBRARIES OpenSSL::Crypto
            )

            # Get version from the static library's path
            execute_process(
                COMMAND "${_OPENSSL_HOMEBREW_PATH}/bin/openssl" version
                OUTPUT_VARIABLE _OPENSSL_VERSION_OUTPUT
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" OPENSSL_VERSION "${_OPENSSL_VERSION_OUTPUT}")

            set(OpenSSL_FOUND TRUE)
            message(STATUS "  ${BoldBlue}OpenSSL${ColorReset} ${BoldGreen}found${ColorReset} (macOS static): ${OPENSSL_VERSION}")
            message(STATUS "    - libcrypto: ${OPENSSL_CRYPTO_STATIC}")
            message(STATUS "    - libssl: ${OPENSSL_SSL_STATIC}")
        endif()
    endif()
endif()

# =============================================================================
# Fallback: Use find_package (dynamic linking)
# =============================================================================
if(NOT _OPENSSL_STATIC_FOUND)
    if(WIN32 AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev"))
        # Force shared OpenSSL on Windows debug/dev builds.
        set(OPENSSL_USE_STATIC_LIBS FALSE)
    endif()
    # Restored CMake caches can refer to dependency files absent on this runner.
    foreach(_openssl_path OPENSSL_ROOT_DIR OPENSSL_INCLUDE_DIR OPENSSL_SSL_LIBRARY OPENSSL_CRYPTO_LIBRARY)
        set(_openssl_probe_path "${${_openssl_path}}")
        if(_openssl_path STREQUAL "OPENSSL_INCLUDE_DIR")
            string(APPEND _openssl_probe_path "/openssl/ssl.h")
        endif()
        if(DEFINED ${_openssl_path} AND NOT "${${_openssl_path}}" STREQUAL "" AND NOT EXISTS "${_openssl_probe_path}")
            unset(${_openssl_path} CACHE)
            unset(${_openssl_path})
        endif()
    endforeach()
    find_package(OpenSSL REQUIRED COMPONENTS SSL Crypto)

    if(OpenSSL_FOUND)
        message(STATUS "  ${BoldBlue}OpenSSL${ColorReset} ${BoldGreen}found${ColorReset}: ${OPENSSL_VERSION}")
    else()
        message(FATAL_ERROR "OpenSSL not found. Please install OpenSSL development libraries.")
    endif()
endif()
