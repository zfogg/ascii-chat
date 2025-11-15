# =============================================================================
# Criterion Test Framework Configuration
# =============================================================================
# Finds and configures Criterion test framework for unit/integration testing
#
# Platform-specific dependency management:
#   - Windows: Tests disabled (Criterion requires pkg-config)
#   - Linux/macOS: Uses pkg-config for system packages
#   - Linux (musl): Tests disabled (Criterion requires glibc)
#
# Prerequisites (must be set before including this file):
#   - WIN32, UNIX: Platform detection variables
#   - USE_MUSL: Whether using musl libc
#   - BUILD_TESTS: Whether to build tests
#
# Outputs (variables set by this file):
#   - CRITERION_FOUND: Whether Criterion was found
#   - BUILD_TESTS: Updated based on platform capabilities
#
# Additional Linux test dependencies (when CRITERION_FOUND):
#   - PROTOBUF_C_*: protobuf-c library
#   - NANOPB_*: nanopb library
#   - BOXFORT_*: boxfort sandboxing library
#   - NANOMSG_*: nanomsg messaging library (optional)
#   - LIBGIT2_*: libgit2 library (optional)
#   - KRB5_GSSAPI_* or LIBSSH2_*: Kerberos/SSH support
# =============================================================================

# Disable tests for musl builds - Criterion test framework requires glibc
# Tests can be run with standard glibc builds instead
if(USE_MUSL)
    set(BUILD_TESTS OFF)
    message(STATUS "Tests disabled for musl builds (${BoldBlue}Criterion${ColorReset} requires ${BoldBlue}glibc${ColorReset})")
endif()

# Windows doesn't use pkg-config, so skip Criterion detection on Windows
# Tests are primarily Unix-based (Criterion requires pkg-config)
if(WIN32 AND BUILD_TESTS)
    message(STATUS "${BoldYellow}Criterion testing framework not found. Tests will not be built.${ColorReset}")
    message(STATUS "${BoldCyan}To run tests on Windows, use Docker:${ColorReset}")
    message(STATUS "  ${BoldWhite}docker-compose -f tests/docker-compose.yml run --rm ascii-chat-tests bash -c 'build_docker/bin/test_unit_defer'${ColorReset}")
    set(BUILD_TESTS OFF)
endif()

if(BUILD_TESTS AND NOT WIN32)
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
