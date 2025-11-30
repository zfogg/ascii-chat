# =============================================================================
# Criterion Test Framework Configuration
# =============================================================================
# Finds and configures Criterion test framework for unit/integration testing
#
# Platform-specific dependency management:
#   - Windows: Criterion tests disabled (requires pkg-config), but lib runtime tests work
#   - Linux/macOS: Uses pkg-config for system packages
#   - Linux (musl): Criterion tests disabled (requires glibc)
#
# Prerequisites (must be set before including this file):
#   - WIN32, UNIX: Platform detection variables
#   - USE_MUSL: Whether using musl libc
#   - BUILD_TESTS: Whether to build tests (user option)
#
# Outputs (variables set by this file):
#   - CRITERION_FOUND: Whether Criterion was found
#   - BUILD_CRITERION_TESTS: Whether to build Criterion-based tests (unit/integration/performance)
#
# Test hierarchy:
#   - BUILD_TESTS: Enables all tests that don't require Criterion (lib runtime tests)
#   - BUILD_CRITERION_TESTS: Enables Criterion-based tests (requires Criterion found)
#
# Additional Linux test dependencies (when CRITERION_FOUND):
#   - PROTOBUF_C_*: protobuf-c library
#   - NANOPB_*: nanopb library
#   - BOXFORT_*: boxfort sandboxing library
#   - NANOMSG_*: nanomsg messaging library (optional)
#   - LIBGIT2_*: libgit2 library (optional)
#   - KRB5_GSSAPI_* or LIBSSH2_*: Kerberos/SSH support
# =============================================================================

# Initialize BUILD_CRITERION_TESTS based on BUILD_TESTS
# This can be disabled even when BUILD_TESTS is ON (e.g., on Windows)
set(BUILD_CRITERION_TESTS ${BUILD_TESTS})

# Disable Criterion tests for musl builds - Criterion test framework requires glibc
# Lib runtime tests (using assert.h) still work
if(USE_MUSL AND BUILD_CRITERION_TESTS)
    set(BUILD_CRITERION_TESTS OFF)
    message(STATUS "Criterion tests disabled for musl builds (${BoldBlue}Criterion${ColorReset} requires ${BoldBlue}glibc${ColorReset})")
    message(STATUS "  -> Library runtime tests still available via ctest")
endif()

# Windows doesn't use pkg-config, so skip Criterion detection on Windows
# Criterion tests are primarily Unix-based, but lib runtime tests work on Windows
if(WIN32 AND BUILD_CRITERION_TESTS)
    message(STATUS "${BoldYellow}Criterion testing framework not available on Windows.${ColorReset}")
    message(STATUS "${BoldCyan}To run Criterion tests, use Docker:${ColorReset}")
    message(STATUS "  ${BoldWhite}docker-compose -f tests/docker-compose.yml run --rm ascii-chat-tests bash -c 'build_docker/bin/test_unit_ascii'${ColorReset}")
    set(BUILD_CRITERION_TESTS OFF)
endif()

if(BUILD_CRITERION_TESTS AND NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CRITERION criterion)

    if(PLATFORM_LINUX AND CRITERION_FOUND)
        # Additional Linux test dependencies (matching Makefile complex setup)
        # These are required by Criterion on Linux for full functionality

        # Protobuf-C
        pkg_check_modules(PROTOBUF_C libprotobuf-c)
        if(NOT PROTOBUF_C_FOUND)
            find_library(PROTOBUF_C_LIBRARIES protobuf-c)
        endif()

        # Nanopb
        pkg_check_modules(NANOPB nanopb)
        if(NOT NANOPB_FOUND)
            find_library(NANOPB_LIBRARIES protobuf-nanopb PATHS /usr/lib/x86_64-linux-gnu)
        endif()

        # Boxfort (sandboxing for criterion)
        pkg_check_modules(BOXFORT boxfort)
        if(NOT BOXFORT_FOUND)
            find_library(BOXFORT_LIBRARIES boxfort)
        endif()

        # Optional: nanomsg, libgit2
        pkg_check_modules(NANOMSG nanomsg)
        pkg_check_modules(LIBGIT2 libgit2)

        # GSSAPI/Kerberos support
        pkg_check_modules(KRB5_GSSAPI krb5-gssapi)
        if(NOT KRB5_GSSAPI_FOUND)
            pkg_check_modules(KRB5_GSSAPI mit-krb5-gssapi)
        endif()
        if(NOT KRB5_GSSAPI_FOUND)
            pkg_check_modules(LIBSSH2 libssh2)
        endif()
    endif()
endif()
