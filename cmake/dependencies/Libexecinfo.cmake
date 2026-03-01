# =============================================================================
# libexecinfo (Backtrace Support) Dependency
# =============================================================================
# Cross-platform configuration for backtrace support
#
# For musl builds: Built from source (musl doesn't have execinfo.h)
# For glibc builds: Uses system libc (has execinfo.h built-in)
#
# Outputs (variables set by this file):
#   - LIBEXECINFO_LIBRARIES: libexecinfo libraries to link (PARENT_SCOPE)
#   - LIBEXECINFO_INCLUDE_DIRS: libexecinfo include directories (PARENT_SCOPE)
# =============================================================================

# Musl build: Build from source since musl doesn't have execinfo.h
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}libexecinfo${ColorReset} from source (musl)...")

    include(ExternalProject)

    set(LIBEXECINFO_PREFIX "${MUSL_DEPS_DIR_STATIC}/libexecinfo")
    set(LIBEXECINFO_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/libexecinfo-build")

    # Only add external project if library doesn't exist
    if(NOT EXISTS "${LIBEXECINFO_PREFIX}/lib/libexecinfo.a")
        message(STATUS "  libexecinfo library not found in cache, will build from source")
        ExternalProject_Add(libexecinfo-musl
            GIT_REPOSITORY https://github.com/mikroskeem/libexecinfo.git
            GIT_TAG master
            PREFIX ${LIBEXECINFO_BUILD_DIR}
            STAMP_DIR ${LIBEXECINFO_BUILD_DIR}/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND ""
            BUILD_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} CFLAGS=-fPIC make -j -C <SOURCE_DIR>
            INSTALL_COMMAND env CC=${MUSL_GCC} make -C <SOURCE_DIR> install PREFIX=${LIBEXECINFO_PREFIX}
            BUILD_IN_SOURCE 1
            BUILD_BYPRODUCTS ${LIBEXECINFO_PREFIX}/lib/libexecinfo.a
            LOG_DOWNLOAD TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}libexecinfo${ColorReset} library found in cache: ${BoldMagenta}${LIBEXECINFO_PREFIX}/lib/libexecinfo.a${ColorReset}")
        # Create a dummy target so dependencies can reference it
        add_custom_target(libexecinfo-musl)
    endif()

    set(LIBEXECINFO_LIBRARIES "${LIBEXECINFO_PREFIX}/lib/libexecinfo.a" PARENT_SCOPE)
    set(LIBEXECINFO_INCLUDE_DIRS "${LIBEXECINFO_PREFIX}/include" PARENT_SCOPE)

    return()
endif()

# Non-musl builds: glibc includes execinfo.h, so no separate library needed
if(UNIX AND NOT APPLE)
    # Linux with glibc: execinfo.h is built-in
    message(STATUS "${BoldGreen}✓${ColorReset} libexecinfo: using glibc built-in (no separate library needed)")
    set(LIBEXECINFO_LIBRARIES "" PARENT_SCOPE)
    set(LIBEXECINFO_INCLUDE_DIRS "" PARENT_SCOPE)

elseif(APPLE)
    # macOS: Has execinfo.h in libc
    message(STATUS "${BoldGreen}✓${ColorReset} libexecinfo: using macOS libc built-in (no separate library needed)")
    set(LIBEXECINFO_LIBRARIES "" PARENT_SCOPE)
    set(LIBEXECINFO_INCLUDE_DIRS "" PARENT_SCOPE)

elseif(WIN32)
    # Windows: No execinfo.h equivalent
    message(STATUS "${BoldYellow}⚠${ColorReset} libexecinfo not needed on Windows")
    set(LIBEXECINFO_LIBRARIES "" PARENT_SCOPE)
    set(LIBEXECINFO_INCLUDE_DIRS "" PARENT_SCOPE)

else()
    message(FATAL_ERROR "Unsupported platform for libexecinfo")
endif()
