# =============================================================================
# Tests Module (Criterion-based tests)
# =============================================================================
# This module sets up the Criterion test framework and creates test executables
#
# Prerequisites:
#   - Libraries created (via Libraries.cmake)
#   - BUILD_CRITERION_TESTS option set (from Criterion.cmake)
#   - CRITERION_FOUND (required for BUILD_CRITERION_TESTS)
#   - Platform detection complete
#   - USE_MUSL known
#
# Outputs:
#   - All test_* executable targets (Criterion-based)
#   - tests, test_debug, test_release, test_all custom targets
#   - tests-timer-start, tests-timer-end (build timing targets)
#   - tests-criterion-build or tests-lib-build (internal build targets)
#
# Note: Library runtime tests (test-static-lib, test-shared-lib) are handled
# by TestLibs.cmake and only require BUILD_TESTS, not Criterion.
#
# Build Timing:
#   When BUILD_TESTS is enabled, the module creates timer targets that measure
#   how long it takes to build all test executables. The timing includes:
#   - All Criterion test executables (when available)
#   - Library runtime tests (test-static-lib, test-shared-lib)
#   - Future defer tests (prepared for in this infrastructure)
# =============================================================================

# Enable CTest if any tests are being built (Criterion or lib runtime)
if(BUILD_TESTS)
    enable_testing()
endif()

# =============================================================================
# Test Build Timing
# =============================================================================
# Create timer start target for all tests (Criterion, library, and future defer tests)
# This target is created unconditionally so other modules can depend on it
if(BUILD_TESTS AND NOT TARGET tests-timer-start)
    add_custom_target(tests-timer-start
        COMMAND ${CMAKE_COMMAND}
            -DACTION=start
            -DTARGET_NAME=tests
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
        COMMENT "Starting test build timing"
        VERBATIM
    )
endif()

# Collect all test targets for timing (populated by Criterion tests, lib tests, etc.)
set(ALL_TIMED_TEST_TARGETS "" CACHE INTERNAL "All test targets for timing")

# Criterion test framework setup
if(BUILD_CRITERION_TESTS AND CRITERION_FOUND)
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

    # Use mimalloc include directories from Mimalloc.cmake
    set(_ascii_tests_mimalloc_include "${MIMALLOC_INCLUDE_DIRS}")

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

        # Add test executable with test utilities
        # - common.c: Provides shared test utilities (test_get_binary_path, etc.)
        # - globals.c: Provides global symbols (g_should_exit) needed by lib code
        # - logging.c: Provides test-specific logging utilities (stdout/stderr redirection)
        add_executable(${test_exe_name} ${test_src} lib/tests/common.c lib/tests/globals.c lib/tests/logging.c)

        # Add timer dependency so timing starts before first test compiles
        if(TARGET tests-timer-start)
            add_dependencies(${test_exe_name} tests-timer-start)
        endif()

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
        # Handle circular dependencies between libraries
        # This is needed because core→network→core and core→crypto have circular refs
        if(NOT WIN32 AND NOT APPLE)
            # Linux: Use --start-group/--end-group to resolve circular dependencies
            # IMPORTANT: All internal libraries must be inside the group, otherwise
            # the linker will process libraries outside the group first and fail
            # to resolve circular references
            target_link_libraries(${test_exe_name}
                -Wl,--start-group
                ascii-chat-simd
                ascii-chat-video
                ascii-chat-audio
                ascii-chat-core
                ascii-chat-panic
                ascii-chat-network
                ascii-chat-crypto
                ascii-chat-platform
                ascii-chat-data-structures
                ascii-chat-util
                -Wl,--end-group
                ${TEST_LDFLAGS}
            )
        elseif(APPLE)
            # macOS: List libraries multiple times (ld64 doesn't have --start-group)
            target_link_libraries(${test_exe_name}
                ascii-chat-lib
                ${TEST_LDFLAGS}
                ascii-chat-core
                ascii-chat-panic
                ascii-chat-util
                ascii-chat-network
                ascii-chat-crypto
            )
        else()
            # Windows
            target_link_libraries(${test_exe_name}
                ascii-chat-lib
                ascii-chat-panic
                ${TEST_LDFLAGS}
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

        # CRITICAL: Disable dead code elimination for test executables
        # Criterion uses __attribute__((constructor)) to register tests, which the linker
        # considers "unused" and removes with --gc-sections/-dead_strip. This causes all
        # tests to be stripped in Release builds, resulting in "Tested: 0" output.
        #
        # On macOS: We cannot use -Wl,-no_dead_strip to disable stripping, so we use
        # -Wl,-all_load on static libraries instead (via target_link_options below).
        # On Linux: Use --no-gc-sections to override the global --gc-sections flag.
        #
        # IMPORTANT: We must NOT replace LINK_OPTIONS entirely on macOS as that would
        # remove sanitizer link flags. Instead we add flags to counteract dead stripping.
        if(NOT WIN32)
            if(APPLE)
                # macOS: Add -all_load to prevent stripping constructor functions from static libs
                # Keep -pie for ASLR security. Don't clear other link options (sanitizers!)
                target_link_options(${test_exe_name} PRIVATE
                    -Wl,-all_load
                )
            else()
                # Linux: Use --no-gc-sections to override the global --gc-sections flag
                target_link_options(${test_exe_name} PRIVATE
                    -Wl,--no-gc-sections
                )
            endif()
        endif()

        # CRITICAL: Disable IPO/LTO for test executables
        # LTO can see that Criterion's __attribute__((constructor)) test registration
        # functions are never called and removes them as dead code. This happens even
        # with --no-gc-sections because LTO operates at a different (interprocedural)
        # level than section-based dead stripping. Without this, Release builds show
        # "Synthesis: Tested: 0" because all test functions are stripped.
        set_target_properties(${test_exe_name} PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION OFF
            INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF
            INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO OFF
        )

        # Add to CTest with TESTING environment variable
        add_test(NAME ${test_exe_name} COMMAND ${test_exe_name})
        set_tests_properties(${test_exe_name} PROPERTIES
            ENVIRONMENT "TESTING=1"
        )
    endforeach()

    # Update timed test targets list with Criterion tests
    set(ALL_TIMED_TEST_TARGETS ${ALL_TEST_TARGETS} CACHE INTERNAL "All test targets for timing")

    # Internal target to build all Criterion tests (without timing)
    add_custom_target(tests-criterion-build
        DEPENDS ${ALL_TEST_TARGETS}
        COMMENT "Building Criterion test executables"
    )

    # Target to build all tests without running them (excluded from default build)
    # This is the main public target that includes timing
    add_custom_target(tests
        COMMENT "Building all test executables"
    )

    # Timer end target runs after all tests are built
    if(TARGET tests-timer-start)
        add_custom_target(tests-timer-end
            COMMAND ${CMAKE_COMMAND}
                -DACTION=end
                -DTARGET_NAME=tests
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
                -DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}
                -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
            COMMENT "Finishing test build timing"
            VERBATIM
        )
        # Timer end depends on timer start and the actual test build
        add_dependencies(tests-timer-end tests-timer-start tests-criterion-build)
        # Main tests target depends on timer end (so timing is shown)
        add_dependencies(tests tests-timer-end)
    else()
        # No timer, just depend on the build target directly
        add_dependencies(tests tests-criterion-build)
    endif()

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

    # Add library test targets to test_all so they're built when running ctest
    # These are defined in TestLibs.cmake but need to be added here after test_all exists
    # Also add them to the timed test targets and timer dependency chain
    if(TARGET test-static-lib)
        add_dependencies(test_all test-static-lib)
        add_dependencies(tests-criterion-build test-static-lib)
        if(TARGET tests-timer-start)
            add_dependencies(test-static-lib tests-timer-start)
        endif()
    endif()
    if(TARGET test-shared-lib)
        add_dependencies(test_all test-shared-lib)
        add_dependencies(tests-criterion-build test-shared-lib)
        if(TARGET tests-timer-start)
            add_dependencies(test-shared-lib tests-timer-start)
        endif()
    endif()

else()
    # Criterion tests not available, but lib runtime tests may still be available
    if(USE_MUSL AND BUILD_TESTS)
        message(STATUS "Criterion tests disabled: System Criterion requires glibc (boxfort dependency)")
        message(STATUS "  -> Library runtime tests still available via ctest")
    elseif(BUILD_TESTS AND NOT WIN32)
        message(STATUS "${Yellow}Criterion testing framework not found. Criterion tests will not be built.${ColorReset}")
        message(STATUS "  -> Library runtime tests still available via ctest")
    endif()

    # Even without Criterion, create tests target for library tests with timing
    if(BUILD_TESTS)
        # Create tests target that depends on lib tests with timing
        add_custom_target(tests-lib-build
            COMMENT "Building library test executables"
        )

        # Add library tests to the build target
        if(TARGET test-static-lib)
            add_dependencies(tests-lib-build test-static-lib)
            if(TARGET tests-timer-start)
                add_dependencies(test-static-lib tests-timer-start)
            endif()
        endif()
        if(TARGET test-shared-lib)
            add_dependencies(tests-lib-build test-shared-lib)
            if(TARGET tests-timer-start)
                add_dependencies(test-shared-lib tests-timer-start)
            endif()
        endif()

        # Create tests target with timing
        add_custom_target(tests
            COMMENT "Building all test executables"
        )

        if(TARGET tests-timer-start)
            add_custom_target(tests-timer-end
                COMMAND ${CMAKE_COMMAND}
                    -DACTION=end
                    -DTARGET_NAME=tests
                    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
                    -DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}
                    -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
                COMMENT "Finishing test build timing"
                VERBATIM
            )
            add_dependencies(tests-timer-end tests-timer-start tests-lib-build)
            add_dependencies(tests tests-timer-end)
        else()
            add_dependencies(tests tests-lib-build)
        endif()

        # Test mode targets
        add_custom_target(test_all
            DEPENDS tests
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMENT "Running all tests"
        )
    endif()
endif()

# =============================================================================
# Standalone Tests (No Criterion Dependency)
# =============================================================================
# Library runtime tests (test-static-lib, test-shared-lib) are handled by
# TestLibs.cmake and only require BUILD_TESTS, not Criterion.
# Note: The defer runtime library was removed when we switched to direct code insertion.

