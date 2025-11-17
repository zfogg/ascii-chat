# =============================================================================
# Tests Module
# =============================================================================
# This module sets up the test framework and creates test executables
#
# Prerequisites:
#   - Libraries created (via Libraries.cmake)
#   - BUILD_TESTS option set
#   - CRITERION_FOUND (if BUILD_TESTS is ON)
#   - Platform detection complete
#   - USE_MUSL known
#
# Outputs:
#   - All test_* executable targets
#   - tests, test_debug, test_release, test_all custom targets
# =============================================================================

# Test framework setup
if(BUILD_TESTS AND CRITERION_FOUND)
    enable_testing()

    # Build test LDFLAGS systematically (matches Makefile complex setup)
    set(TEST_LDFLAGS ${CRITERION_LIBRARIES})
    # Criterion requires libffi for theories support
    list(APPEND TEST_LDFLAGS ffi)

    if(PLATFORM_LINUX)
        # Add Linux-specific test dependencies (matching Makefile order)
        if(PROTOBUF_C_FOUND)
            list(APPEND TEST_LDFLAGS ${PROTOBUF_C_LIBRARIES})
        elseif(PROTOBUF_C_LIBRARIES)
            list(APPEND TEST_LDFLAGS ${PROTOBUF_C_LIBRARIES})
        else()
            # Protobuf-C is optional - only add if library exists
            find_library(PROTOBUF_C_LIB protobuf-c)
            if(PROTOBUF_C_LIB)
                list(APPEND TEST_LDFLAGS protobuf-c)
            endif()
        endif()

        if(NANOPB_FOUND)
            list(APPEND TEST_LDFLAGS ${NANOPB_LIBRARIES})
        elseif(NANOPB_LIBRARIES)
            list(APPEND TEST_LDFLAGS ${NANOPB_LIBRARIES})
        elseif(EXISTS /usr/lib/x86_64-linux-gnu/libprotobuf-nanopb.a)
            list(APPEND TEST_LDFLAGS /usr/lib/x86_64-linux-gnu/libprotobuf-nanopb.a)
        endif()

        # Boxfort (subprocess isolation) - skip for musl builds (requires glibc)
        if(NOT USE_MUSL)
            if(BOXFORT_FOUND)
                list(APPEND TEST_LDFLAGS ${BOXFORT_LIBRARIES})
            elseif(BOXFORT_LIBRARIES)
                list(APPEND TEST_LDFLAGS ${BOXFORT_LIBRARIES})
            else()
                # Boxfort is optional - only add if library exists
                find_library(BOXFORT_LIB boxfort)
                if(BOXFORT_LIB)
                    list(APPEND TEST_LDFLAGS boxfort)
                endif()
            endif()
        endif()

        # Optional dependencies (skip for musl static builds to avoid dynamic library issues)
        if(NOT USE_MUSL)
            if(NANOMSG_FOUND)
                list(APPEND TEST_LDFLAGS ${NANOMSG_LIBRARIES})
            endif()
            if(LIBGIT2_FOUND)
                list(APPEND TEST_LDFLAGS ${LIBGIT2_LIBRARIES})

                # libgit2 requires OpenSSL, libssh2, and http_parser when statically linked
                find_package(OpenSSL)
                if(OpenSSL_FOUND)
                    list(APPEND TEST_LDFLAGS OpenSSL::SSL OpenSSL::Crypto)
                else()
                    # Fallback to direct library names
                    list(APPEND TEST_LDFLAGS ssl crypto)
                endif()

            # Additional libgit2 dependencies (required for static linking)
            # These must be linked unconditionally when using static libgit2
            # Check if each library exists before adding

            # On Linux, provide search hints for standard locations
            if(PLATFORM_LINUX)
                find_library(SSH2_LIB ssh2 HINTS /usr/lib/x86_64-linux-gnu /usr/lib /lib)
                find_library(HTTP_PARSER_LIB http_parser HINTS /usr/lib/x86_64-linux-gnu /usr/lib /lib)
                find_library(PCRE2_LIB pcre2-8 HINTS /usr/lib/x86_64-linux-gnu /usr/lib /lib)
            else()
                find_library(SSH2_LIB ssh2)
                find_library(HTTP_PARSER_LIB http_parser)
                find_library(PCRE2_LIB pcre2-8)
            endif()

            # Only link libgit2 dependencies if they exist (make them optional)
            if(SSH2_LIB)
                list(APPEND TEST_LDFLAGS ssh2)
                message(STATUS "Added libssh2 for tests")
            else()
                message(WARNING "libssh2 not found - some git features may not work")
            endif()

            if(HTTP_PARSER_LIB)
                list(APPEND TEST_LDFLAGS http_parser)
                message(STATUS "Added http_parser for tests")
            else()
                message(WARNING "http_parser not found - some git features may not work")
            endif()

            if(PCRE2_LIB)
                list(APPEND TEST_LDFLAGS pcre2-8)
                message(STATUS "Added pcre2-8 for tests")
            else()
                message(WARNING "pcre2-8 not found - some git features may not work")
            endif()

            # libgit2 also requires zlib for compression
            find_library(ZLIB_LIB z HINTS /usr/lib/x86_64-linux-gnu /usr/lib /lib)
            if(ZLIB_LIB)
                list(APPEND TEST_LDFLAGS z)
                message(STATUS "Added zlib for tests (libgit2 dependency)")
            else()
                message(WARNING "zlib not found - libgit2 compression may not work")
            endif()

            if(NOT SSH2_LIB AND NOT HTTP_PARSER_LIB AND NOT PCRE2_LIB)
                message(WARNING "No libgit2 dependencies found - git features will be disabled")
            endif()
            endif() # LIBGIT2_FOUND
        endif() # NOT USE_MUSL

        # GSSAPI/Kerberos support (skip for musl to avoid dynamic library issues)
        if(NOT USE_MUSL)
            if(KRB5_GSSAPI_FOUND)
                list(APPEND TEST_LDFLAGS ${KRB5_GSSAPI_LIBRARIES})
            elseif(LIBSSH2_FOUND)
                list(APPEND TEST_LDFLAGS ${LIBSSH2_LIBRARIES})
            else()
                list(APPEND TEST_LDFLAGS gssapi_krb5 krb5 k5crypto com_err)
            endif()
        endif()

        # Additional system libraries (matches Makefile)
        # Make these optional - only link if available
        # Note: ssh2, http_parser, and pcre2-8 are already handled above with libgit2
        foreach(lib dl resolv)
            find_library(${lib}_LIB ${lib})
            if(${lib}_LIB)
                list(APPEND TEST_LDFLAGS ${lib})
            endif()
        endforeach()

    elseif(PLATFORM_DARWIN)
        # macOS test linking (simpler, matches Makefile)
        if(NOT CRITERION_LIBRARIES)
            # Fallback for Homebrew (Criterion is keg-only, so provide both opt prefixes)
            list(APPEND TEST_LDFLAGS
                "-L/opt/homebrew/opt/criterion/lib"
                "-L/usr/local/opt/criterion/lib"
                "-L/opt/homebrew/lib"
                "-L/usr/local/lib"
                criterion
            )
        endif()
    endif()

    # Determine mimalloc include directories for tests (tests include common.h directly)
    set(_ascii_tests_mimalloc_include "")
    if(TARGET mimalloc-static)
        get_target_property(_ascii_mimalloc_iface mimalloc-static INTERFACE_INCLUDE_DIRECTORIES)
        if(_ascii_mimalloc_iface)
            list(APPEND _ascii_tests_mimalloc_include ${_ascii_mimalloc_iface})
        endif()
    endif()
    if(DEFINED MIMALLOC_SOURCE_DIR AND EXISTS "${MIMALLOC_SOURCE_DIR}/include")
        list(APPEND _ascii_tests_mimalloc_include "${MIMALLOC_SOURCE_DIR}/include")
    endif()
    if(DEFINED MIMALLOC_INCLUDE_DIR AND MIMALLOC_INCLUDE_DIR)
        list(APPEND _ascii_tests_mimalloc_include "${MIMALLOC_INCLUDE_DIR}")
    endif()
    list(REMOVE_DUPLICATES _ascii_tests_mimalloc_include)

    # Find test files (excluding problematic ones, matches Makefile)
    file(GLOB_RECURSE TEST_SRCS_ALL tests/unit/*.c tests/integration/*.c tests/performance/*.c)
    set(TEST_EXCLUDES
        tests/integration/server_multiclient_test.c
        tests/integration/video_pipeline_test.c
    )

    set(TEST_SRCS)
    foreach(test_src IN LISTS TEST_SRCS_ALL)
        list(FIND TEST_EXCLUDES ${test_src} IS_EXCLUDED)
        if(IS_EXCLUDED EQUAL -1)
            list(APPEND TEST_SRCS ${test_src})
        endif()
    endforeach()

    # Create test executables (matches Makefile naming convention)
    set(ALL_TEST_TARGETS)
    foreach(test_src ${TEST_SRCS})
        # Transform test file paths to executable names with flattened structure
        # tests/unit/common_test.c -> test_unit_common
        # tests/integration/crypto_network_test.c -> test_integration_crypto_network
        # Determine the top-level test category (unit/integration/performance)
        file(RELATIVE_PATH test_rel ${PROJECT_SOURCE_DIR}/tests ${test_src})
        string(REPLACE "\\" "/" test_rel ${test_rel})
        string(REGEX MATCH "^[^/]+" test_category ${test_rel})
        if(NOT test_category)
            message(FATAL_ERROR "Failed to determine test category for ${test_src}")
        endif()

        # Create a flattened executable name that preserves subdirectory information
        string(REPLACE ".c" "" test_rel_noext ${test_rel})
        string(REPLACE "/" "_" test_rel_flat ${test_rel_noext})
        string(REGEX REPLACE "_test$" "" test_rel_flat ${test_rel_flat})
        set(test_exe_name "test_${test_rel_flat}")
        list(APPEND ALL_TEST_TARGETS ${test_exe_name})

        # Add test executable with test utilities (EXCLUDE_FROM_ALL = not built by default)
        # - globals.c: Provides global symbols (g_should_exit) needed by lib code
        # - logging.c: Provides test-specific logging utilities (stdout/stderr redirection)
        add_executable(${test_exe_name} EXCLUDE_FROM_ALL ${test_src} lib/tests/globals.c lib/tests/logging.c)

        # Disable precompiled headers for test targets to avoid conflicts with Criterion macros
        set_target_properties(${test_exe_name} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

        # Add Criterion include directories
        target_include_directories(${test_exe_name} PRIVATE ${CRITERION_INCLUDE_DIRS} ${_ascii_tests_mimalloc_include})

        # Define CRITERION_TEST for test environment detection
        target_compile_definitions(${test_exe_name} PRIVATE CRITERION_TEST=1)

        # For musl builds, disable subprocess isolation (no forking)
        if(USE_MUSL)
            target_compile_definitions(${test_exe_name} PRIVATE CRITERION_NO_EARLY_EXIT=1)
        endif()

        # Link test dependencies (order matters for linking)
        target_link_libraries(${test_exe_name}
            ascii-chat-lib
            ${TEST_LDFLAGS}
        )

        # Handle circular dependencies between libraries
        # This is needed because core→network→core and core→crypto have circular refs
        if(NOT WIN32 AND NOT APPLE)
            # Linux: Use --start-group/--end-group to resolve circular dependencies
            target_link_libraries(${test_exe_name}
                -Wl,--start-group
                ascii-chat-core
                ascii-chat-debug
                ascii-chat-network
                ascii-chat-crypto
                -Wl,--end-group
            )
        elseif(APPLE)
            # macOS: List libraries multiple times (ld64 doesn't have --start-group)
            target_link_libraries(${test_exe_name}
                ascii-chat-core
                ascii-chat-debug
                ascii-chat-util
                ascii-chat-network
                ascii-chat-crypto
            )
        endif()

        # For musl static builds, allow undefined boxfort references (they won't be called)
        if(USE_MUSL)
            target_link_options(${test_exe_name} PRIVATE
                -Wl,--allow-shlib-undefined
                -Wl,--unresolved-symbols=ignore-all
            )
        endif()

        # Use release objects for performance tests
        if(test_category STREQUAL "performance")
            target_compile_options(${test_exe_name} PRIVATE -O3 -DNDEBUG)
        endif()

        # Add to CTest
        add_test(NAME ${test_exe_name} COMMAND ${test_exe_name})
    endforeach()

    # Target to build all tests without running them (excluded from default build)
    add_custom_target(tests
        DEPENDS ${ALL_TEST_TARGETS}
        COMMENT "Building all test executables"
    )

    # Custom targets for different test modes (matches Makefile)
    add_custom_target(test_debug
        DEPENDS tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMENT "Running tests in debug mode"
    )

    add_custom_target(test_release
        DEPENDS tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C Release
        COMMENT "Running tests in release mode"
    )

    # Overall test target
    add_custom_target(test_all
        DEPENDS tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMENT "Running all tests"
    )

else()
    if(USE_MUSL AND BUILD_TESTS)
        message(STATUS "Tests disabled: System Criterion requires glibc (boxfort dependency)")
        message(STATUS "  -> Use glibc build for tests: cmake -B build -DBUILD_TESTS=ON")
        message(STATUS "  -> Musl builds are for deployment, not testing")
    elseif(BUILD_TESTS)
        message(STATUS "${Yellow}Criterion testing framework not found. Tests will not be built.${ColorReset}")
    endif()
endif()

# =============================================================================
# Standalone Tests (No Criterion Dependency)
# =============================================================================
# These tests use standard assert.h and don't require the Criterion framework

# Defer Runtime Test - validates LIFO cleanup ordering and memory management
if(EXISTS "${PROJECT_SOURCE_DIR}/lib/tooling/defer/defer.c")
    add_executable(test-defer EXCLUDE_FROM_ALL
        tests/unit/tooling/defer_minimal_test.c
    )
    target_include_directories(test-defer PRIVATE
        ${PROJECT_SOURCE_DIR}/lib
    )
    # Link against the shared library for SAFE_MALLOC and log_* macros
    # Note: defer.c is NOT compiled directly here to avoid duplicate symbols
    target_link_libraries(test-defer
        ascii-chat-shared
    )
    # Always link against ascii-chat-defer to get defer runtime functions
    # If defer.c is already in ascii-chat-shared (when ASCII_DEFER_ENABLED is TRUE),
    # the linker will use those symbols and ignore duplicates from ascii-chat-defer
    if(TARGET ascii-chat-defer)
        target_link_libraries(test-defer
            ascii-chat-defer
        )
    endif()
    set_target_properties(test-defer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    message(STATUS "Added standalone test: test-defer")
endif()

