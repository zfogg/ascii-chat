# =============================================================================
# libsodium-bcrypt-pbkdf Configuration Module
# =============================================================================
# This module handles libsodium-bcrypt-pbkdf dependency patching and setup.
#
# Note: Windows sys/param.h fix is now in the forked submodule
#       (zfogg/libsodium-bcrypt-pbkdf, fix-windows-sys-param branch)
#
# Prerequisites:
#   - cmake/utils/Patching.cmake must be included
#
# Outputs:
#   - Applies libsodium-bcrypt-pbkdf-0-fix-ubsan.patch if needed
#   - LIBSODIUM_BCRYPT_PBKDF_LIBRARY: Built library for iOS (if applicable)
#   - LIBSODIUM_BCRYPT_PBKDF_INCLUDE_DIR: Include directory
# =============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/utils/Patching.cmake)

# iOS: Build libsodium-bcrypt-pbkdf as static library
if(PLATFORM_IOS)
    message(STATUS "Configuring ${BoldBlue}libsodium-bcrypt-pbkdf${ColorReset} from source (iOS cross-compile)...")

    set(LIBSODIUM_BCRYPT_PBKDF_PREFIX "${IOS_DEPS_CACHE_DIR}/libsodium-bcrypt-pbkdf")
    set(LIBSODIUM_BCRYPT_PBKDF_LIBRARY "${LIBSODIUM_BCRYPT_PBKDF_PREFIX}/lib/libsodium-bcrypt-pbkdf.a")
    set(LIBSODIUM_BCRYPT_PBKDF_INCLUDE_DIR "${LIBSODIUM_BCRYPT_PBKDF_PREFIX}/include")

    if(NOT EXISTS "${LIBSODIUM_BCRYPT_PBKDF_LIBRARY}")
        message(STATUS "  libsodium-bcrypt-pbkdf library not found in cache, will build from source")

        # Get iOS SDK path
        if(BUILD_IOS_SIM)
            execute_process(COMMAND xcrun --sdk iphonesimulator --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
        else()
            execute_process(COMMAND xcrun --sdk iphoneos --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()

        set(DEP_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libsodium-bcrypt-pbkdf")

        message(STATUS "  Building libsodium-bcrypt-pbkdf for iOS...")
        execute_process(
            COMMAND bash -c "cd '${DEP_DIR}' && \
                    env CC=clang \
                    AR=ar \
                    CFLAGS='-fPIC -isysroot ${IOS_SDK_PATH} -arch arm64 -miphoneos-version-min=16.0' \
                    OBJCOPY=objcopy \
                    HAVE_EXPLICIT_BZERO=0 \
                    make lib && \
                    mkdir -p '${LIBSODIUM_BCRYPT_PBKDF_PREFIX}/lib' '${LIBSODIUM_BCRYPT_PBKDF_PREFIX}/include' && \
                    cp src/libsodium-bcrypt-pbkdf.a '${LIBSODIUM_BCRYPT_PBKDF_PREFIX}/lib/' && \
                    cp include/*.h '${LIBSODIUM_BCRYPT_PBKDF_PREFIX}/include/'"
            RESULT_VARIABLE BUILD_RESULT
            OUTPUT_VARIABLE BUILD_OUTPUT
            ERROR_VARIABLE BUILD_ERROR
        )
        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "libsodium-bcrypt-pbkdf iOS build failed:\\n${BUILD_ERROR}")
        endif()
    else()
        message(STATUS "  ${BoldBlue}libsodium-bcrypt-pbkdf${ColorReset} library found in iOS cache: ${BoldMagenta}${LIBSODIUM_BCRYPT_PBKDF_LIBRARY}${ColorReset}")
    endif()

    # Create imported target for iOS
    if(NOT TARGET libsodium_bcrypt_pbkdf::libsodium_bcrypt_pbkdf)
        add_library(libsodium_bcrypt_pbkdf::libsodium_bcrypt_pbkdf STATIC IMPORTED GLOBAL)
        set_target_properties(libsodium_bcrypt_pbkdf::libsodium_bcrypt_pbkdf PROPERTIES
            IMPORTED_LOCATION "${LIBSODIUM_BCRYPT_PBKDF_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBSODIUM_BCRYPT_PBKDF_INCLUDE_DIR}"
        )
    endif()

    message(STATUS "${BoldGreen}✓${ColorReset} libsodium-bcrypt-pbkdf (iOS): ${LIBSODIUM_BCRYPT_PBKDF_LIBRARY}")
    return()
endif()

function(configure_libsodium_bcrypt_pbkdf)
    set(DEP_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libsodium-bcrypt-pbkdf")
    set(PATCHES_DIR "${CMAKE_SOURCE_DIR}/cmake/deps/patches")

    # Apply patch #0: UBSAN fix (all platforms)
    apply_patch(
        TARGET_DIR "${DEP_DIR}"
        PATCH_FILE "${PATCHES_DIR}/libsodium-bcrypt-pbkdf-0-fix-ubsan.patch"
        PATCH_NUM 0
        DESCRIPTION "Fix undefined behavior sanitizer warnings"
        ASSUME_UNCHANGED
            src/openbsd-compat/blowfish.c
            src/openbsd-compat/bcrypt_pbkdf.c
    )

    # Verify the source files exist
    if(NOT EXISTS "${DEP_DIR}/src/openbsd-compat/blowfish.c")
        message(FATAL_ERROR "libsodium-bcrypt-pbkdf source file not found")
    endif()
endfunction()
