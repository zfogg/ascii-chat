# =============================================================================
# MuslDependencies.cmake - Build all dependencies from source for musl
# =============================================================================
# When USE_MUSL=ON, we can't use system libraries (glibc-based). Instead, we
# build all dependencies from source and cache them in .deps-cache-musl/.
#
# Dependencies built from source:
#   - zlib (compression)
#   - libsodium (crypto)
#   - PortAudio (audio I/O)
#   - BearSSL (TLS for SSH key fetching)
#
# All cached in ${FETCHCONTENT_BASE_DIR} to persist across build/ deletions.
# =============================================================================

if(NOT USE_MUSL)
    return()
endif()

message(STATUS "Building dependencies from source for musl libc...")

include(FetchContent)

# Save current output directories
set(_SAVED_ARCHIVE_OUTPUT_DIR ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})
set(_SAVED_LIBRARY_OUTPUT_DIR ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

# =============================================================================
# zlib - Compression library
# =============================================================================
message(STATUS "Configuring zlib from source...")

FetchContent_Declare(
    zlib_musl
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
    GIT_SHALLOW TRUE
    SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/zlib-src"
    BINARY_DIR "${FETCHCONTENT_BASE_DIR}/zlib-build"
)

# Configure zlib options
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${FETCHCONTENT_BASE_DIR}/zlib-build/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${FETCHCONTENT_BASE_DIR}/zlib-build/lib")

FetchContent_MakeAvailable(zlib_musl)

set(ZLIB_FOUND TRUE)
set(ZLIB_LIBRARIES zlibstatic)
set(ZLIB_INCLUDE_DIRS "${FETCHCONTENT_BASE_DIR}/zlib-src;${FETCHCONTENT_BASE_DIR}/zlib-build")

# =============================================================================
# libsodium - Cryptography library
# =============================================================================
message(STATUS "Configuring libsodium from source...")

FetchContent_Declare(
    libsodium_musl
    GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
    GIT_TAG 1.0.20-RELEASE
    GIT_SHALLOW TRUE
    SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/libsodium-src"
    BINARY_DIR "${FETCHCONTENT_BASE_DIR}/libsodium-build"
)

# libsodium uses autotools, so we need to build it manually
FetchContent_GetProperties(libsodium_musl)
if(NOT libsodium_musl_POPULATED)
    FetchContent_Populate(libsodium_musl)

    # Build libsodium using its configure script
    set(LIBSODIUM_INSTALL_DIR "${FETCHCONTENT_BASE_DIR}/libsodium-install")

    if(NOT EXISTS "${LIBSODIUM_INSTALL_DIR}/lib/libsodium.a")
        message(STATUS "Building libsodium...")
        execute_process(
            COMMAND ./autogen.sh
            WORKING_DIRECTORY "${libsodium_musl_SOURCE_DIR}"
            RESULT_VARIABLE LIBSODIUM_AUTOGEN_RESULT
        )

        if(LIBSODIUM_AUTOGEN_RESULT EQUAL 0)
            execute_process(
                COMMAND ./configure --prefix=${LIBSODIUM_INSTALL_DIR} --enable-static --disable-shared
                WORKING_DIRECTORY "${libsodium_musl_SOURCE_DIR}"
                RESULT_VARIABLE LIBSODIUM_CONFIGURE_RESULT
            )

            if(LIBSODIUM_CONFIGURE_RESULT EQUAL 0)
                execute_process(
                    COMMAND make -j${CMAKE_BUILD_PARALLEL_LEVEL}
                    WORKING_DIRECTORY "${libsodium_musl_SOURCE_DIR}"
                    RESULT_VARIABLE LIBSODIUM_MAKE_RESULT
                )

                if(LIBSODIUM_MAKE_RESULT EQUAL 0)
                    execute_process(
                        COMMAND make install
                        WORKING_DIRECTORY "${libsodium_musl_SOURCE_DIR}"
                    )
                endif()
            endif()
        endif()
    else()
        message(STATUS "Using cached libsodium build")
    endif()
endif()

set(LIBSODIUM_FOUND TRUE)
set(LIBSODIUM_LIBRARIES "${LIBSODIUM_INSTALL_DIR}/lib/libsodium.a")
set(LIBSODIUM_INCLUDE_DIRS "${LIBSODIUM_INSTALL_DIR}/include")

# =============================================================================
# PortAudio - Audio I/O library
# =============================================================================
message(STATUS "Configuring PortAudio from source...")

FetchContent_Declare(
    portaudio_musl
    GIT_REPOSITORY https://github.com/PortAudio/portaudio.git
    GIT_TAG v19.7.0
    GIT_SHALLOW TRUE
    SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/portaudio-src"
    BINARY_DIR "${FETCHCONTENT_BASE_DIR}/portaudio-build"
)

# PortAudio CMake options
set(PA_BUILD_SHARED OFF CACHE BOOL "Build shared library")
set(PA_BUILD_STATIC ON CACHE BOOL "Build static library")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${FETCHCONTENT_BASE_DIR}/portaudio-build/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${FETCHCONTENT_BASE_DIR}/portaudio-build/lib")

FetchContent_MakeAvailable(portaudio_musl)

set(PORTAUDIO_FOUND TRUE)
set(PORTAUDIO_LIBRARIES portaudio_static)
set(PORTAUDIO_INCLUDE_DIRS "${FETCHCONTENT_BASE_DIR}/portaudio-src/include")

# =============================================================================
# BearSSL - TLS library for SSH key fetching
# =============================================================================
message(STATUS "Configuring BearSSL from source...")

# BearSSL doesn't use CMake, so we build it manually
set(BEARSSL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/bearssl")
set(BEARSSL_BUILD_DIR "${FETCHCONTENT_BASE_DIR}/bearssl-build")
set(BEARSSL_LIB "${BEARSSL_BUILD_DIR}/libbearssl.a")

if(EXISTS "${BEARSSL_SOURCE_DIR}")
    if(NOT EXISTS "${BEARSSL_LIB}")
        message(STATUS "Building BearSSL...")
        file(MAKE_DIRECTORY "${BEARSSL_BUILD_DIR}")

        execute_process(
            COMMAND make
            WORKING_DIRECTORY "${BEARSSL_SOURCE_DIR}"
            RESULT_VARIABLE BEARSSL_MAKE_RESULT
        )

        if(BEARSSL_MAKE_RESULT EQUAL 0)
            # Copy library to cache
            file(COPY "${BEARSSL_SOURCE_DIR}/build/libbearssl.a"
                 DESTINATION "${BEARSSL_BUILD_DIR}")
        endif()
    else()
        message(STATUS "Using cached BearSSL build")
    endif()

    set(BEARSSL_FOUND TRUE)
    set(BEARSSL_LIBRARIES "${BEARSSL_LIB}")
    set(BEARSSL_INCLUDE_DIRS "${BEARSSL_SOURCE_DIR}/inc")
else()
    message(WARNING "BearSSL submodule not found - GitHub/GitLab key fetching will be disabled")
    set(BEARSSL_FOUND FALSE)
    set(BEARSSL_LIBRARIES "")
    set(BEARSSL_INCLUDE_DIRS "")
endif()

# Restore output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${_SAVED_ARCHIVE_OUTPUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${_SAVED_LIBRARY_OUTPUT_DIR})

message(STATUS "All musl dependencies configured and cached")
