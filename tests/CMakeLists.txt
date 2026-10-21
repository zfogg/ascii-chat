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
# Criterion Test Configuration
# =============================================================================
# Configure Criterion-specific test options based on build type.
# Coverage builds are slower due to instrumentation, so we use longer timeouts.
# XML output is generated for coverage builds to capture detailed test results.

# Detect coverage build via ASCIICHAT_ENABLE_COVERAGE option
set(IS_COVERAGE_BUILD OFF)
if(ASCIICHAT_ENABLE_COVERAGE)
    set(IS_COVERAGE_BUILD ON)
endif()

# Configure timeouts based on build type
if(IS_COVERAGE_BUILD)
    set(CRITERION_TIMEOUT 60)      # Criterion's internal timeout per test case
    set(CTEST_TEST_TIMEOUT 120)    # CTest's overall timeout per test executable
    set(CRITERION_JOBS 1)          # Disable internal parallelism for coverage accuracy
    set(CTEST_PARALLEL_JOBS 2)     # Limit parallel tests to prevent resource exhaustion during coverage
else()
    set(CRITERION_TIMEOUT 25)      # Default timeout
    set(CTEST_TEST_TIMEOUT 30)     # Tests must complete in 30 seconds or less
    # Use all CPU cores minus 1 to prevent system saturation
    math(EXPR CRITERION_JOBS "${CPU_CORES} - 1")
    set(CTEST_PARALLEL_JOBS "${CPU_CORES} - 1")
endif()

# XML output directory for Criterion test results
set(CRITERION_XML_DIR "${CMAKE_BINARY_DIR}/Testing/criterion-xml")
file(MAKE_DIRECTORY ${CRITERION_XML_DIR})

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
        elseif(CMAKE_LIBRARY_ARCHITECTURE AND EXISTS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/libprotobuf-nanopb.a)
            list(APPEND TEST_LDFLAGS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/libprotobuf-nanopb.a)
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

                # libgit2 requires OpenSSL and libssh2 when statically linked
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
                    if(CMAKE_LIBRARY_ARCHITECTURE)
                        find_library(SSH2_LIB ssh2 HINTS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE} /usr/lib /lib)
                        find_library(PCRE2_LIB pcre2-8 HINTS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE} /usr/lib /lib)
                    else()
                        find_library(SSH2_LIB ssh2 HINTS /usr/lib /lib)
                        find_library(PCRE2_LIB pcre2-8 HINTS /usr/lib /lib)
                    endif()
                else()
                    find_library(SSH2_LIB ssh2)
                    find_library(PCRE2_LIB pcre2-8)
                endif()

                # Only link libgit2 dependencies if they exist (make them optional)
                if(SSH2_LIB)
                    list(APPEND TEST_LDFLAGS ssh2)
                    message(STATUS "Added libssh2 for tests")
                else()
                    message(WARNING "libssh2 not found - some git features may not work")
                endif()

                if(PCRE2_LIB)
                    list(APPEND TEST_LDFLAGS pcre2-8)
                    message(STATUS "Added pcre2-8 for tests")
                else()
                    message(WARNING "pcre2-8 not found - some git features may not work")
                endif()

                # libgit2 also requires zlib for compression
                if(CMAKE_LIBRARY_ARCHITECTURE)
                    find_library(ZLIB_LIB z HINTS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE} /usr/lib /lib)
                else()
                    find_library(ZLIB_LIB z HINTS /usr/lib /lib)
                endif()
                if(ZLIB_LIB)
                    list(APPEND TEST_LDFLAGS z)
                    message(STATUS "Added zlib for tests (libgit2 dependency)")
                else()
                    message(WARNING "zlib not found - libgit2 compression may not work")
                endif()

                if(NOT SSH2_LIB AND NOT PCRE2_LIB)
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
        # Note: ssh2 and and pcre2-8 are already handled above with libgit2
        foreach(lib dl resolv)
            find_library(${lib}_LIB ${lib})
            if(${lib}_LIB)
                list(APPEND TEST_LDFLAGS ${lib})
            endif()
        endforeach()

    elseif(PLATFORM_DARWIN)
        # macOS test linking - must add library directories from pkg-config
        # pkg-config sets CRITERION_LIBRARY_DIRS but CMake doesn't automatically use it
        # This is especially important when using Homebrew LLVM which doesn't search
        # /opt/homebrew/lib by default
        if(CRITERION_LIBRARY_DIRS)
            foreach(dir ${CRITERION_LIBRARY_DIRS})
                list(APPEND TEST_LDFLAGS "-L${dir}")
            endforeach()
        else()
            # Fallback for Homebrew (Criterion is keg-only, so provide both opt prefixes)
            if(HOMEBREW_PREFIX)
                list(APPEND TEST_LDFLAGS
                    "-L${HOMEBREW_PREFIX}/opt/criterion/lib"
                    "-L${HOMEBREW_PREFIX}/lib"
                )
            endif()
            list(APPEND TEST_LDFLAGS "-L/usr/local/lib")
        endif()
        list(APPEND TEST_LDFLAGS "-framework SystemConfiguration")
        # Note: CRITERION_LIBRARIES is already added to TEST_LDFLAGS on line 87,
        # so we only add the library search directories here
    endif()

    # Use mimalloc include directories from Mimalloc.cmake
    set(_ascii_tests_mimalloc_include "${MIMALLOC_INCLUDE_DIRS}")

    # Find test files (excluding problematic ones, matches Makefile)
    file(GLOB_RECURSE TEST_SRCS_ALL ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.c ${CMAKE_CURRENT_SOURCE_DIR}/integration/*.c ${CMAKE_CURRENT_SOURCE_DIR}/performance/*.c)
    # GLOB_RECURSE returns absolute paths, so TEST_EXCLUDES must also be absolute
    set(TEST_EXCLUDES
        # Native UI fixture has its own main and is driven by the tmux harness.
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/ui_native_probe.c
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/sync_regressions.c
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/source_paths.c
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/client_like_lifecycle.c
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/server_multiclient_test.c
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/video_pipeline_test.c
    )
    # Exclude query tests if query runtime library not available
    if(NOT TARGET ascii-query-runtime)
        list(APPEND TEST_EXCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/unit/tooling/query_test.c)
    endif()

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
        file(RELATIVE_PATH test_rel ${CMAKE_CURRENT_SOURCE_DIR} ${test_src})
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
        add_executable(${test_exe_name} ${test_src} ${CMAKE_SOURCE_DIR}/lib/tests/common.c ${CMAKE_SOURCE_DIR}/lib/tests/globals.c ${CMAKE_SOURCE_DIR}/lib/tests/logging.c)

        # Configure BUILD_RPATH to prefer locally built libraries over system-installed ones
        # This ensures tests use the correct libasciichat from build/lib instead of /usr/local/lib
        # Priority: local build lib > LLVM libs > system libs
        if(APPLE)
            set_target_properties(${test_exe_name} PROPERTIES
                BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY};${CMAKE_BUILD_RPATH};${HOMEBREW_PREFIX}/lib;/usr/local/lib"
                MACOSX_RPATH ON
            )
        elseif(TARGET ascii-chat-shared)
            set_target_properties(${test_exe_name} PROPERTIES
                BUILD_RPATH "$<TARGET_FILE_DIR:ascii-chat-shared>;${CMAKE_BUILD_RPATH}"
            )
        endif()

        # Add timer dependency so timing starts before first test compiles
        if(TARGET tests-timer-start)
            add_dependencies(${test_exe_name} tests-timer-start)
        endif()

        # Disable precompiled headers for test targets to avoid conflicts with Criterion macros
        set_target_properties(${test_exe_name} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

        # Add Criterion and project include directories
        target_include_directories(${test_exe_name} PRIVATE
            ${CRITERION_INCLUDE_DIRS}
            ${_ascii_tests_mimalloc_include}
            ${LIBWEBSOCKETS_INCLUDE_DIRS}
        )

        # Define CRITERION_TEST for test environment detection
        target_compile_definitions(${test_exe_name} PRIVATE CRITERION_TEST=1)

        # For musl builds, disable subprocess isolation (no forking)
        if(USE_MUSL)
            target_compile_definitions(${test_exe_name} PRIVATE CRITERION_NO_EARLY_EXIT=1)
        endif()

        # Link test dependencies via shared library + external dependencies
        # Ensure shared library is built first (not available on all platforms)
        if(TARGET ascii-chat-shared)
            set(_ascii_tests_library ascii-chat-shared)
            add_dependencies(${test_exe_name} ascii-chat-shared)
        else()
            set(_ascii_tests_library ascii-chat-static)
            add_dependencies(${test_exe_name} build-ascii-chat-static-lib)
        endif()

        # Link against shared library which contains all code
        target_link_libraries(${test_exe_name}
            ${_ascii_tests_library}
            ${TEST_LDFLAGS}
        )

        if(USE_MIMALLOC)
            if(TARGET mimalloc-static)
                target_link_libraries(${test_exe_name} mimalloc-static)
            elseif(MIMALLOC_LIBRARIES)
                target_link_libraries(${test_exe_name} ${MIMALLOC_LIBRARIES})
            endif()
        endif()

        # Coverage builds need the profile runtime linked into test executables
        if(IS_COVERAGE_BUILD)
            target_compile_options(${test_exe_name} PRIVATE --coverage)
            target_link_options(${test_exe_name} PRIVATE --coverage)
            # Clang with lld needs explicit profile runtime
            if(NOT DEFINED _CLANG_PROFILE_LIB)
                # Try architecture-specific name first, then platform name
                if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
                    set(_profile_names "libclang_rt.profile_osx.a" "libclang_rt.profile-arm64.a")
                else()
                    set(_profile_names "libclang_rt.profile-x86_64.a" "libclang_rt.profile_osx.a")
                endif()
                set(_CLANG_PROFILE_LIB "")
                foreach(_name ${_profile_names})
                    execute_process(
                        COMMAND ${CMAKE_C_COMPILER} --print-file-name=${_name}
                        OUTPUT_VARIABLE _candidate
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                    )
                    if(EXISTS "${_candidate}")
                        set(_CLANG_PROFILE_LIB "${_candidate}")
                        break()
                    endif()
                endforeach()
                if(EXISTS "${_CLANG_PROFILE_LIB}")
                    message(STATUS "Found Clang profile runtime: ${_CLANG_PROFILE_LIB}")
                    set(_CLANG_PROFILE_LIB "${_CLANG_PROFILE_LIB}" CACHE INTERNAL "")
                else()
                    message(WARNING "Clang profile runtime not found: ${_CLANG_PROFILE_LIB}")
                    set(_CLANG_PROFILE_LIB "" CACHE INTERNAL "")
                endif()
            endif()
            if(_CLANG_PROFILE_LIB)
                target_link_libraries(${test_exe_name} "${_CLANG_PROFILE_LIB}")
            endif()
        endif()

        # The regex fixtures call PCRE2 directly to prepare and inspect native patterns.
        if(test_exe_name MATCHES "^test_unit_util_regex_(fallback|match)$")
            target_include_directories(${test_exe_name} PRIVATE ${PCRE2_INCLUDE_DIRS})
            target_link_libraries(${test_exe_name} ${PCRE2_LIBRARIES})
        endif()

        # External dependencies needed because shared library has them as undefined symbols
        # (normal for dylibs - symbols resolved at runtime)
        target_link_libraries(${test_exe_name}
            ${LIBSODIUM_LIBRARIES}
            ${ZSTD_LIBRARIES}
            ${OPUS_LIBRARIES}
            ${SQLITE3_LIBRARIES}
            ${LIBWEBSOCKETS_LIBRARIES}
            z  # zlib needed for libwebsockets compression support
            libdatachannel
            OpenSSL::Crypto
            -lc++  # C++ stdlib needed for libdatachannel
            ${FFMPEG_LIBRARIES}
        )
        if(APPLE)
            target_link_libraries(${test_exe_name}
                ${PORTAUDIO_LIBRARIES}
                ${COREAUDIO_FRAMEWORK}
                ${AUDIOUNIT_FRAMEWORK}
                ${AUDIOTOOLBOX_FRAMEWORK}
                ${CORESERVICES_FRAMEWORK}
            )
        elseif(PLATFORM_LINUX AND JACK_LIB)
            target_link_libraries(${test_exe_name}
                ${PORTAUDIO_LIBRARIES}
                ${JACK_LIB}
            )
        else()
            target_link_libraries(${test_exe_name} ${PORTAUDIO_LIBRARIES})
        endif()
        if(MINIUPNPC_FOUND)
            target_include_directories(${test_exe_name} PRIVATE ${MINIUPNPC_INCLUDE_DIRS})
            target_link_libraries(${test_exe_name} ${MINIUPNPC_LIBRARIES})
            if(NATPMP_LIBRARY)
                target_include_directories(${test_exe_name} PRIVATE ${NATPMP_INCLUDE_DIR})
                target_link_libraries(${test_exe_name} ${NATPMP_LIBRARY})
            endif()
        endif()
        if(BEARSSL_FOUND)
            target_link_libraries(${test_exe_name} ${BEARSSL_LIBRARIES})
        endif()

        # Add query runtime library if available
        if(TARGET ascii-query-runtime)
            target_link_libraries(${test_exe_name} ascii-query-runtime)
        endif()

        # Apply sanitizers to test binaries (needed when loading sanitized shared library)
        apply_sanitizers_to_target(${test_exe_name})

        # For musl static builds, allow undefined boxfort references (they won't be called)
        if(USE_MUSL)
            target_link_options(${test_exe_name} PRIVATE
                -Wl,--allow-shlib-undefined
                -Wl,--unresolved-symbols=ignore-all
            )
            # Link Alpine libc++ for musl builds (function defined in Musl.cmake)
            link_alpine_libcxx(${test_exe_name})
        endif()

        # Use release objects for performance tests
        if(test_category STREQUAL "performance")
            target_compile_options(${test_exe_name} PRIVATE -O3 -DNDEBUG)
        endif()

        # Disable dead code elimination for test executables
        # Interface link options are appended after private options, so the
        # shared project policy must omit stripping for Criterion targets.
        set_property(TARGET ${test_exe_name} PROPERTY ASCIICHAT_KEEP_TEST_REGISTRATION TRUE)
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
                # The global -undefined dynamic_lookup flag handles undefined symbols from WebRTC
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

        # Disable IPO/LTO for test executables
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

        # Build Criterion command-line arguments
        # Integration tests may launch several servers and clients in child
        # processes.  Keep their Criterion cases serial within the executable;
        # CTest's RUN_SERIAL property cannot prevent Criterion itself from
        # running those cases concurrently.
        set(_test_criterion_jobs ${CRITERION_JOBS})
        if(test_category STREQUAL "integration" OR test_exe_name MATCHES "gpg")
            set(_test_criterion_jobs 1)
        endif()

        set(_test_criterion_timeout ${CRITERION_TIMEOUT})
        if(test_exe_name MATCHES "webrtc_frame_capture")
            set(_test_criterion_timeout 60)
        endif()

        set(_test_ctest_timeout ${CTEST_TEST_TIMEOUT})
        if(test_category STREQUAL "integration")
            # Integration executables contain several serial process-level
            # cases; the timeout applies to the complete executable.
            set(_test_ctest_timeout 120)
        endif()

        # BoxFort's concurrent timeout queue can lose pending requests when
        # different deadlines are inserted. These suites mix per-case timeouts
        # with the default limit; serialize them to retain every timeout and
        # allow LeakSanitizer to check the runner without suppressions.
        if(test_exe_name MATCHES "^test_unit_(audio_recording|stats_stats)$")
            set(_test_criterion_jobs 1)
            set(_test_ctest_timeout 120)
        endif()

        if(test_exe_name STREQUAL "test_unit_options_options")
            # The CLI suite launches many sanitized parse/help cases. Preserve
            # their case deadlines while allowing the complete suite to finish.
            set(_test_ctest_timeout 120)
        endif()

        if(test_exe_name STREQUAL "test_unit_discovery_session_registry")
            # Durable SQLite fixtures compete for fsync when their cases run
            # concurrently. Keep each case's deadline and serialize the suite
            # so late completions cannot race BoxFort's timeout notifications.
            set(_test_criterion_jobs 1)
            set(_test_ctest_timeout 120)
        endif()

        if(test_exe_name MATCHES "stream_resolution_e2e")
            # This suite runs many serial network cases with bounded per-request timeouts.
            set(_test_ctest_timeout 600)
        endif()

        set(_criterion_args
            --jobs ${_test_criterion_jobs}
            --timeout ${_test_criterion_timeout}
            --color=always
            --short-filename
        )

        # Add XML output for all builds (needed for CI test reporting)
        list(APPEND _criterion_args --xml=${CRITERION_XML_DIR}/${test_exe_name}.xml)

        if(test_exe_name MATCHES "main_integration")
            # Each case launches independent server/client subprocesses. Run
            # them in separate CTest processes so a prior case cannot retain
            # child-process or socket state in the Criterion runner.
            set(_main_integration_cases
                client_main_help_flag
                client_main_no_server
                server_client_basic_connection
                server_client_with_options
                server_main_help_flag
                server_main_invalid_port
                server_main_starts_and_stops
                server_multiple_clients_concurrent
                server_multiple_clients_sequential
                server_survives_client_crash
            )
            foreach(_case IN LISTS _main_integration_cases)
                set(_case_test_name "${test_exe_name}_${_case}")
                add_test(NAME ${_case_test_name}
                    COMMAND ${test_exe_name} ${_criterion_args} --filter "*${_case}*"
                )
                set_tests_properties(${_case_test_name} PROPERTIES
                    ENVIRONMENT "TESTING=1;CRITERION_TEST=1;BUILD_DIR=${CMAKE_BINARY_DIR}"
                    TIMEOUT ${_test_ctest_timeout}
                    LABELS "${test_category}"
                    RUN_SERIAL TRUE
                )
            endforeach()
        else()
            # Add to CTest with Criterion flags
            add_test(NAME ${test_exe_name}
                COMMAND ${test_exe_name} ${_criterion_args}
            )

            # Set test properties
            set_tests_properties(${test_exe_name} PROPERTIES
                ENVIRONMENT "TESTING=1;CRITERION_TEST=1;BUILD_DIR=${CMAKE_BINARY_DIR}"
                TIMEOUT ${_test_ctest_timeout}
                LABELS "${test_category}"
            )
            if(test_category STREQUAL "integration")
                # Process fixtures share listening ports and temporary resources.
                set_tests_properties(${test_exe_name} PROPERTIES RUN_SERIAL TRUE)
            endif()
        endif()
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
    # NOTE: Only available with --preset github (CI environment)
    # For local development, use individual test targets (test_debug, test_release, etc.)
    # Claude should NOT run the 'tests' target directly - run individual tests instead
    if(DEFINED ENV{GITHUB_CMAKE_BINARY_DIR})
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
    else()
        # For non-GitHub presets, print a helpful message
        message(STATUS "Note: The 'tests' target is only available with --preset github")
        message(STATUS "For local development, use individual test targets like:")
        message(STATUS "  cmake --build build --target test_debug")
        message(STATUS "  cmake --build build --target test_release")
    endif()

    # Custom targets for different test modes (matches Makefile)
    # Use CTEST_PARALLEL_JOBS which is limited to prevent resource exhaustion
    add_custom_target(test_debug
        DEPENDS tests-criterion-build
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --parallel ${CTEST_PARALLEL_JOBS}
        COMMENT "Running tests in debug mode (${CTEST_PARALLEL_JOBS} parallel jobs)"
    )

    add_custom_target(test_release
        DEPENDS tests-criterion-build
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C Release --parallel ${CTEST_PARALLEL_JOBS}
        COMMENT "Running tests in release mode (${CTEST_PARALLEL_JOBS} parallel jobs)"
    )

    # Overall test target
    add_custom_target(test_all
        DEPENDS tests-criterion-build
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --parallel ${CTEST_PARALLEL_JOBS}
        COMMENT "Running all tests (${CTEST_PARALLEL_JOBS} parallel jobs)"
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
        # Use CPU_CORES from Init.cmake for automatic parallel job count
        add_custom_target(test_all
            DEPENDS tests
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --parallel ${CPU_CORES}
            COMMENT "Running all tests (${CPU_CORES} parallel jobs)"
        )
    endif()
endif()

# =============================================================================
# Standalone Tests (No Criterion Dependency)
# =============================================================================
# Library runtime tests (test-static-lib, test-shared-lib) are handled by
# TestLibs.cmake and only require BUILD_TESTS, not Criterion.
# Note: The defer runtime library was removed when we switched to direct code insertion.
# =============================================================================
# Test Library Executables
# =============================================================================
# Simple test programs to verify both static and shared libraries work correctly
# Both targets use the same source file but link against different libraries
#
# These tests use assert.h (not Criterion), so they only require BUILD_TESTS=ON
# and work on all platforms including Windows.
#
# Prerequisites:
#   - BUILD_TESTS option set
#   - Libraries created (via Libraries.cmake)
#
# Outputs:
#   - test-static-lib: Tests the static library (when available)
#   - test-shared-lib: Tests the shared library (when available)
# =============================================================================

# Use mimalloc include directories from Mimalloc.cmake
set(_asciichat_mimalloc_include "${MIMALLOC_INCLUDE_DIRS}")

# =============================================================================
# Test Static Library
# =============================================================================
# Only available when building STATIC libraries (not OBJECT libraries)
# On Windows, static libs are built in Release mode (OBJECT libs in Debug/Dev)
if(NOT BUILDING_OBJECT_LIBS)
    if(BUILD_TESTS)
        add_executable(test-static-lib ${CMAKE_SOURCE_DIR}/cmake/test/test_lib.c)
        set_target_properties(test-static-lib PROPERTIES LINKER_LANGUAGE CXX)
        set(TEST_STATIC_DEFAULT "built by default")
    else()
        add_executable(test-static-lib EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/cmake/test/test_lib.c)
        set_target_properties(test-static-lib PROPERTIES LINKER_LANGUAGE CXX)
        set(TEST_STATIC_DEFAULT "explicit target only")
    endif()

    # Link against the static library (mimalloc is transitively linked)
    target_link_libraries(test-static-lib PRIVATE
        ascii-chat-static-lib
    )

    # Link panic runtime when panic instrumentation is enabled
    if(ASCIICHAT_BUILD_WITH_PANIC AND TARGET ascii-panic-runtime)
        target_link_libraries(test-static-lib PRIVATE ascii-panic-runtime)
    endif()

    # Add musl dependency ordering for proper build sequencing
    if(USE_MUSL)
        add_dependencies(test-static-lib portaudio-musl alsa-lib-musl libsodium-musl zstd-musl libexecinfo-musl opus-musl)
        # Link Alpine libc++ for musl builds (function defined in Musl.cmake)
        link_alpine_libcxx(test-static-lib)
    endif()

    # Include necessary headers
    target_include_directories(test-static-lib PRIVATE
        ${CMAKE_SOURCE_DIR}/lib
        ${CMAKE_SOURCE_DIR}/lib/platform
        ${CMAKE_BINARY_DIR}/generated
        ${_asciichat_mimalloc_include}
    )

    # Set output directory
    set_target_properties(test-static-lib PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    # Simple runtime check for the static library
    add_custom_target(run-test-static-lib
        COMMAND ${CMAKE_BINARY_DIR}/bin/test-static-lib
        DEPENDS test-static-lib
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running test-static-lib to verify static library works"
        VERBATIM
    )

    # Register as ctest test (only when BUILD_TESTS is enabled)
    if(BUILD_TESTS)
        add_test(NAME static-lib-runtime
            COMMAND ${CMAKE_BINARY_DIR}/bin/test-static-lib)
    endif()

    message(STATUS "Added test-static-lib target to test libasciichat.a (${TEST_STATIC_DEFAULT})")
else()
    message(STATUS "Skipping test-static-lib (not available with OBJECT libraries)")
endif()

# =============================================================================
# Test Shared Library
# =============================================================================
if(BUILD_TESTS)
    add_test(NAME cmake-patching
        COMMAND "${CMAKE_COMMAND}" "-DPATCH_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/patching"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patching.cmake")
    set_tests_properties(cmake-patching PROPERTIES LABELS "unit" TIMEOUT 30)
    add_test(NAME cmake-version
        COMMAND "${CMAKE_COMMAND}" "-DVERSION_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/version"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cmake")
    set_tests_properties(cmake-version PROPERTIES LABELS "unit" TIMEOUT 180)
endif()

# Available when shared library is built (skipped for musl and Linux Release non-musl)
if(TARGET ascii-chat-shared)
    add_executable(test-source-paths EXCLUDE_FROM_ALL integration/source_paths.c)
    target_link_libraries(test-source-paths PRIVATE ascii-chat-shared)
    apply_sanitizers_to_target(test-source-paths)
    target_include_directories(test-source-paths PRIVATE ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    set_target_properties(test-source-paths PROPERTIES
        LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_TESTS)
        add_test(NAME source-paths COMMAND test-source-paths "${CMAKE_CURRENT_BINARY_DIR}/source-paths.log")
        set_tests_properties(source-paths PROPERTIES LABELS "integration" TIMEOUT 30)
        foreach(test_build_target tests tests-criterion-build tests-lib-build)
            if(TARGET ${test_build_target})
                add_dependencies(${test_build_target} test-source-paths)
            endif()
        endforeach()
    endif()

    add_executable(test-sync-regressions EXCLUDE_FROM_ALL integration/sync_regressions.c)
    target_link_libraries(test-sync-regressions PRIVATE ascii-chat-shared)
    apply_sanitizers_to_target(test-sync-regressions)
    target_include_directories(test-sync-regressions PRIVATE ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    set_target_properties(test-sync-regressions PROPERTIES
        LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_TESTS)
        add_test(NAME sync-regressions COMMAND test-sync-regressions sync
            "${CMAKE_CURRENT_BINARY_DIR}/sync-regressions.log")
        set_tests_properties(sync-regressions PROPERTIES TIMEOUT 60)
    endif()
    if(BUILD_TESTS)
        add_executable(test-shared-lib ${CMAKE_SOURCE_DIR}/cmake/test/test_lib.c)
        set_target_properties(test-shared-lib PROPERTIES LINKER_LANGUAGE CXX)
        set(TEST_SHARED_DEFAULT "built by default")
    else()
        add_executable(test-shared-lib EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/cmake/test/test_lib.c)
        set_target_properties(test-shared-lib PROPERTIES LINKER_LANGUAGE CXX)
        set(TEST_SHARED_DEFAULT "explicit target only")
    endif()

    # Link against the shared library (mimalloc is in the shared library)
    target_link_libraries(test-shared-lib PRIVATE
        ascii-chat-shared
    )

    # Include necessary headers
    target_include_directories(test-shared-lib PRIVATE
        ${CMAKE_SOURCE_DIR}/lib
        ${CMAKE_SOURCE_DIR}/lib/platform
        ${CMAKE_BINARY_DIR}/generated
        ${_asciichat_mimalloc_include}
    )

    # Set output directory
    set_target_properties(test-shared-lib PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    # Make sure shared library is built first
    add_dependencies(test-shared-lib ascii-chat-shared)

    message(STATUS "Added test-shared-lib target to test asciichat.dll/.so (${TEST_SHARED_DEFAULT})")

    # Add a custom test that runs test-shared-lib to verify it works
    add_custom_target(run-test-shared-lib
        COMMAND ${CMAKE_BINARY_DIR}/bin/test-shared-lib
        DEPENDS test-shared-lib
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running test-shared-lib to verify shared library works"
        VERBATIM
    )

    # Register as ctest test (only when BUILD_TESTS is enabled)
    if(BUILD_TESTS)
        add_test(NAME shared-lib-runtime
            COMMAND ${CMAKE_BINARY_DIR}/bin/test-shared-lib)
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            execute_process(
                COMMAND ${CMAKE_C_COMPILER} --print-file-name=libclang_rt.asan-x86_64.so
                OUTPUT_VARIABLE ASAN_RUNTIME_LIBRARY
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(EXISTS "${ASAN_RUNTIME_LIBRARY}")
                set_tests_properties(shared-lib-runtime PROPERTIES
                    ENVIRONMENT "LD_PRELOAD=${ASAN_RUNTIME_LIBRARY}")
            endif()
        endif()
    endif()
else()
    message(STATUS "Skipping test-shared-lib (not compatible with musl static builds)")
endif()

# Uses the production library; does not require Criterion (including on Windows).
if(TARGET ascii-chat-static)
    add_executable(test-crypto-logging EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/tests/standalone/logging/crypto_logging.c)
    target_link_libraries(test-crypto-logging PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    apply_sanitizers_to_target(test-crypto-logging)
    target_include_directories(test-crypto-logging PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
    set_target_properties(test-crypto-logging PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_TESTS)
        if(TARGET tests)
            add_dependencies(tests test-crypto-logging)
        endif()
        add_test(NAME crypto-logging COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/standalone/logging/run.py $<TARGET_FILE:test-crypto-logging>)
        set_tests_properties(crypto-logging PROPERTIES TIMEOUT 60)
        if(WIN32)
            set_property(TEST crypto-logging PROPERTY ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        endif()
    endif()

    add_executable(test-errno-stacks EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/tests/standalone/errno/errno_test.c)
    target_link_libraries(test-errno-stacks PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    apply_sanitizers_to_target(test-errno-stacks)
    target_include_directories(test-errno-stacks PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
    set_target_properties(test-errno-stacks PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_TESTS)
        if(TARGET tests)
            add_dependencies(tests test-errno-stacks)
        endif()
        add_test(NAME errno-stacks COMMAND test-errno-stacks)
        set_tests_properties(errno-stacks PROPERTIES TIMEOUT 60)
    endif()
endif()

# Runs the production allocator and queue on every native platform, without Criterion.
if(TARGET ascii-chat-static)
    add_executable(test-buffer-ownership EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/tests/standalone/buffer/buffer_test.c)
    target_link_libraries(test-buffer-ownership PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES}
        ${LIBWEBSOCKETS_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto ${ZLIB_LIBRARIES})
    if(WIN32)
        target_link_libraries(test-buffer-ownership PRIVATE ws2_32)
    elseif(APPLE)
        target_link_libraries(test-buffer-ownership PRIVATE "-framework CoreFoundation" "-framework SystemConfiguration")
    endif()
    apply_sanitizers_to_target(test-buffer-ownership)
    target_include_directories(test-buffer-ownership PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated
        ${LIBWEBSOCKETS_INCLUDE_DIRS})
    set_target_properties(test-buffer-ownership PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_TESTS)
        if(TARGET tests)
            add_dependencies(tests test-buffer-ownership)
        endif()
        add_test(NAME buffer-ownership COMMAND test-buffer-ownership)
        set_tests_properties(buffer-ownership PROPERTIES TIMEOUT 120)
    endif()
endif()

# This executable is driven by tmux_prompts.py/tmux_capture.py, not Criterion.
if(TARGET ascii-chat-static)
    add_executable(ui-native-probe EXCLUDE_FROM_ALL integration/ui_native_probe.c)
    target_include_directories(ui-native-probe PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated)
    target_link_libraries(ui-native-probe PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    if(WIN32)
        target_link_libraries(ui-native-probe PRIVATE ws2_32)
    endif()
    apply_sanitizers_to_target(ui-native-probe)
    set_target_properties(ui-native-probe PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    foreach(test_target tests tests-criterion-build)
        if(TARGET ${test_target})
            add_dependencies(${test_target} ui-native-probe test-buffer-ownership test-errno-stacks)
            if(TARGET test-sync-regressions)
                add_dependencies(${test_target} test-sync-regressions test-shared-lib)
            endif()
        endif()
    endforeach()
endif()

# The lifecycle probe takes scenario names, not Criterion command-line options.
if(BUILD_TESTS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_dependency_paths_args)
    if(WIN32)
        list(APPEND _dependency_paths_args
            --nmake "${ASCIICHAT_NMAKE_EXECUTABLE}"
            --clang-cl "${ASCIICHAT_CLANG_CL_EXECUTABLE}"
            --llvm-lib "${ASCIICHAT_LLVM_LIB_EXECUTABLE}")
    endif()
    add_test(NAME dependency-paths COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/standalone/dependency_paths/run.py ${_dependency_paths_args})
    set_tests_properties(dependency-paths PROPERTIES LABELS "unit;build" TIMEOUT 60)
    if(NOT WIN32)
        add_test(NAME websocket-dependency-probes COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/standalone/websocket_probes/run.py)
        set_tests_properties(websocket-dependency-probes PROPERTIES LABELS "unit;build" TIMEOUT 60)
    endif()
    add_executable(test-client-like-lifecycle integration/client_like_lifecycle.c)
    if(TARGET ascii-chat-shared)
        target_link_libraries(test-client-like-lifecycle PRIVATE ascii-chat-shared)
    else()
        target_link_libraries(test-client-like-lifecycle PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    endif()
    target_include_directories(test-client-like-lifecycle PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_BINARY_DIR}/generated
        ${_asciichat_mimalloc_include})
    apply_sanitizers_to_target(test-client-like-lifecycle)
    set_target_properties(test-client-like-lifecycle PROPERTIES
        LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    # Cover both the documented tests target and the Criterion run targets.
    foreach(test_build_target tests tests-criterion-build tests-lib-build)
        if(TARGET ${test_build_target})
            add_dependencies(${test_build_target} test-client-like-lifecycle)
        endif()
    endforeach()
    add_test(NAME client-like-lifecycle
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/integration/client_like_lifecycle.py
            $<TARGET_FILE:test-client-like-lifecycle>)
    set_tests_properties(client-like-lifecycle PROPERTIES
        LABELS "integration" TIMEOUT 120)
endif()

# Exercises the production discovery state machine without external services.
if(BUILD_TESTS)
    add_executable(test-discovery-session-process standalone/discovery/session_process_test.c)
    if(TARGET ascii-chat-shared)
        target_link_libraries(test-discovery-session-process PRIVATE ascii-chat-shared ${MIMALLOC_LIBRARIES})
    else()
        target_link_libraries(test-discovery-session-process PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    endif()
    target_include_directories(test-discovery-session-process PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_BINARY_DIR}/generated
        ${_asciichat_mimalloc_include})
    if(WIN32)
        target_link_libraries(test-discovery-session-process PRIVATE ws2_32)
    endif()
    apply_sanitizers_to_target(test-discovery-session-process)
    set_target_properties(test-discovery-session-process PROPERTIES
        LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    foreach(test_build_target tests tests-criterion-build tests-lib-build)
        if(TARGET ${test_build_target})
            add_dependencies(${test_build_target} test-discovery-session-process)
        endif()
    endforeach()
    add_test(NAME discovery-session-process COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tests/integration/discovery_session_process.py
        $<TARGET_FILE:test-discovery-session-process>)
    set_tests_properties(discovery-session-process PROPERTIES LABELS "integration" TIMEOUT 60)
endif()

if(BUILD_TESTS AND TARGET ascii-chat)
    add_test(NAME discovery-direct-tcp COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/discovery_direct_tcp.py $<TARGET_FILE:ascii-chat>)
    set_tests_properties(discovery-direct-tcp PROPERTIES LABELS "integration" TIMEOUT 90)
    foreach(test_build_target tests tests-criterion-build tests-lib-build)
        if(TARGET ${test_build_target})
            add_dependencies(${test_build_target} ascii-chat)
        endif()
    endforeach()
    add_test(NAME server-lifecycle
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/integration/server_lifecycle.py
            $<TARGET_FILE:ascii-chat>)
    set_tests_properties(server-lifecycle PROPERTIES LABELS "integration" TIMEOUT 180)
endif()

if(BUILD_TESTS AND TARGET ascii-chat)
    add_test(NAME server-client-lifecycle
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/integration/server_client_lifecycle.py
            $<TARGET_FILE:ascii-chat>)
    set_tests_properties(server-client-lifecycle PROPERTIES LABELS "integration" TIMEOUT 180)
endif()

# Production transport tests run natively on Windows, macOS, and Linux without Criterion.
if(BUILD_TESTS AND TARGET ascii-chat AND NOT EMSCRIPTEN)
    add_test(NAME event-io-integration COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/event_io.py $<TARGET_FILE:ascii-chat>)
    # The admission stress fixture opens 90 peers and performs serial crypto
    # handshakes. Give it the same isolation as the Criterion process fixtures.
    set_tests_properties(event-io-integration PROPERTIES LABELS "integration" TIMEOUT 180 RUN_SERIAL TRUE)
endif()

if(TARGET ascii-chat-static AND NOT EMSCRIPTEN)
    add_executable(test-webrtc-security EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/tests/standalone/network/webrtc_security_test.c)
    target_link_libraries(test-webrtc-security PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${LIBSODIUM_LIBRARIES})
    if(WIN32)
        target_link_libraries(test-webrtc-security PRIVATE ws2_32)
    endif()
    apply_sanitizers_to_target(test-webrtc-security)
    target_include_directories(test-webrtc-security PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
    set_target_properties(test-webrtc-security PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_TESTS)
        add_test(NAME webrtc-security COMMAND test-webrtc-security)
        set_tests_properties(webrtc-security PROPERTIES TIMEOUT 60)
        if(WIN32)
            set_property(TEST webrtc-security PROPERTY ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
                "PATH=path_list_prepend:${ASCIICHAT_SANITIZER_RUNTIME_DIR}")
        endif()
        if(TARGET tests)
            add_dependencies(tests test-webrtc-security)
        endif()
    endif()
    add_executable(bench-acip-prepare EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/tests/benchmarks/acip_prepare.c)
    target_link_libraries(bench-acip-prepare PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    if(WIN32)
        target_link_libraries(bench-acip-prepare PRIVATE ws2_32)
    endif()
    apply_sanitizers_to_target(bench-acip-prepare)
    target_include_directories(bench-acip-prepare PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
    set_target_properties(bench-acip-prepare PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    add_executable(test-acip-prepare EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/tests/standalone/network/prepare_test.c)
    target_link_libraries(test-acip-prepare PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${ASCIICHAT_LIBUV_TARGET})
    apply_sanitizers_to_target(test-acip-prepare)
    target_include_directories(test-acip-prepare PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
    set_target_properties(test-acip-prepare PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_TESTS)
        set_property(TARGET test-acip-prepare PROPERTY EXCLUDE_FROM_ALL FALSE)
        add_test(NAME acip-prepare COMMAND test-acip-prepare)
        add_test(NAME acip-prepare-websocket COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/integration/acip_prepare_websocket.py $<TARGET_FILE:test-acip-prepare>)
        set_tests_properties(acip-prepare acip-prepare-websocket PROPERTIES TIMEOUT 60)
        if(WIN32)
            set_property(TEST acip-prepare acip-prepare-websocket PROPERTY ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
                "PATH=path_list_prepend:${ASCIICHAT_SANITIZER_RUNTIME_DIR}")
        endif()
        if(TARGET tests)
            add_dependencies(tests test-acip-prepare)
        endif()
    endif()
    add_executable(test-network-events EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tests/standalone/network/event_test.c
        ${CMAKE_SOURCE_DIR}/tests/standalone/network/accepted_start_test.c)
    target_link_libraries(test-network-events PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${ASCIICHAT_LIBUV_TARGET})
    apply_sanitizers_to_target(test-network-events)
    target_include_directories(test-network-events PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
    set_target_properties(test-network-events PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_TESTS)
        add_test(NAME network-events COMMAND test-network-events)
        set_tests_properties(network-events PROPERTIES TIMEOUT 60)
        if(WIN32)
            set_property(TEST network-events PROPERTY ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
                "PATH=path_list_prepend:${ASCIICHAT_SANITIZER_RUNTIME_DIR}")
        endif()
        if(TARGET tests)
            add_dependencies(tests test-network-events)
        endif()
    endif()
    add_executable(test-network-connections EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/tests/standalone/network/connection_test.c)
    target_link_libraries(test-network-connections PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${ASCIICHAT_LIBUV_TARGET})
    apply_sanitizers_to_target(test-network-connections)
    target_include_directories(test-network-connections PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
    set_target_properties(test-network-connections PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_TESTS)
        add_test(NAME network-connections COMMAND test-network-connections)
        set_tests_properties(network-connections PROPERTIES TIMEOUT 60)
        if(WIN32)
            set_property(TEST network-connections PROPERTY ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
                "PATH=path_list_prepend:${ASCIICHAT_SANITIZER_RUNTIME_DIR}")
        endif()
        if(TARGET tests)
            add_dependencies(tests test-network-connections)
        endif()
    endif()
endif()

if(TARGET ascii-chat-static)
    add_executable(test-network-vectors EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/tests/standalone/network/vectored_test.c)
    target_link_libraries(test-network-vectors PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    if(WIN32)
        target_link_libraries(test-network-vectors PRIVATE ws2_32)
    endif()
    apply_sanitizers_to_target(test-network-vectors)
    target_include_directories(test-network-vectors PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
    set_target_properties(test-network-vectors PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_TESTS)
        if(TARGET tests)
            add_dependencies(tests test-network-vectors)
        endif()
        add_test(NAME network-vectors COMMAND test-network-vectors)
        set_tests_properties(network-vectors PROPERTIES TIMEOUT 120)
        if(WIN32)
            set_property(TEST network-vectors PROPERTY ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
                "PATH=path_list_prepend:${ASCIICHAT_SANITIZER_RUNTIME_DIR}")
        endif()
    endif()
endif()

if(BUILD_TESTS AND TARGET ascii-chat)
    add_test(NAME manpage-generation
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/integration/manpage_generation.py
            $<TARGET_FILE:ascii-chat>)
    set_tests_properties(manpage-generation PROPERTIES LABELS "integration" TIMEOUT 120)
endif()

if(TARGET ascii-chat-static)
    add_executable(test-crypto-policy EXCLUDE_FROM_ALL standalone/crypto/policy_test.c)
    target_link_libraries(test-crypto-policy PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    target_include_directories(test-crypto-policy PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
    apply_sanitizers_to_target(test-crypto-policy)
    set_target_properties(test-crypto-policy PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_TESTS)
        add_test(NAME crypto-policy COMMAND test-crypto-policy)
        if(WIN32)
            set_property(TEST crypto-policy PROPERTY ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
                "PATH=path_list_prepend:${ASCIICHAT_SANITIZER_RUNTIME_DIR}")
        endif()

        if(TARGET tests)
            add_dependencies(tests test-crypto-policy)
        endif()
    endif()
endif()

# Shared discovery host rendering, including builds without Criterion.
if(TARGET ascii-chat-static AND NOT EMSCRIPTEN)
    add_executable(test-host-render EXCLUDE_FROM_ALL standalone/session/host_render_test.c)
    target_link_libraries(test-host-render PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    if(WIN32)
        target_link_libraries(test-host-render PRIVATE ws2_32)
    endif()
    target_include_directories(test-host-render PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    apply_sanitizers_to_target(test-host-render)
    set_target_properties(test-host-render PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_TESTS)
        set_property(TARGET test-host-render PROPERTY EXCLUDE_FROM_ALL FALSE)
        add_test(NAME host-render COMMAND test-host-render)
        set_tests_properties(host-render PROPERTIES LABELS "integration" TIMEOUT 30)
        if(WIN32)
            set_property(TEST host-render PROPERTY ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
                "PATH=path_list_prepend:${ASCIICHAT_SANITIZER_RUNTIME_DIR}")
        endif()
        foreach(test_build_target tests tests-criterion-build tests-lib-build)
            if(TARGET ${test_build_target})
                add_dependencies(${test_build_target} test-host-render)
            endif()
        endforeach()
    endif()
endif()

# Fault injection around real participant workers; runs without Criterion on Windows too.
if(BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(test-review-findings standalone/review_findings_test.c)
    target_link_libraries(test-review-findings PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    target_include_directories(test-review-findings PRIVATE ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    if(WIN32)
        target_link_libraries(test-review-findings PRIVATE ws2_32)
    endif()
    apply_sanitizers_to_target(test-review-findings)
    set_target_properties(test-review-findings PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(TARGET tests)
        add_dependencies(tests test-review-findings)
    endif()
    add_test(NAME review-findings COMMAND test-review-findings)
    set_tests_properties(review-findings PROPERTIES LABELS "unit" TIMEOUT 60)
    add_executable(test-runtime-settings standalone/options/runtime_test.c)
    target_link_libraries(test-runtime-settings PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    target_include_directories(test-runtime-settings PRIVATE ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    if(WIN32)
        target_link_libraries(test-runtime-settings PRIVATE ws2_32)
    endif()
    apply_sanitizers_to_target(test-runtime-settings)
    set_target_properties(test-runtime-settings PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(TARGET tests)
        add_dependencies(tests test-runtime-settings)
    endif()
    add_test(NAME runtime-settings COMMAND test-runtime-settings)
    set_tests_properties(runtime-settings PROPERTIES LABELS "unit" TIMEOUT 60)
endif()

if(BUILD_TESTS)
    add_executable(test-participant-media standalone/session/participant_media_test.c)
    target_link_libraries(test-participant-media PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${LIBSODIUM_LIBRARIES})
    target_include_directories(test-participant-media PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    if(WIN32)
        target_link_libraries(test-participant-media PRIVATE ws2_32)
    endif()
    apply_sanitizers_to_target(test-participant-media)
    set_target_properties(test-participant-media PROPERTIES
        LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    foreach(test_build_target tests tests-criterion-build tests-lib-build)
        if(TARGET ${test_build_target})
            add_dependencies(${test_build_target} test-participant-media)
        endif()
    endforeach()
    add_test(NAME participant-media COMMAND test-participant-media)
    set_tests_properties(participant-media PROPERTIES LABELS "integration" TIMEOUT 60)
endif()

if(BUILD_TESTS AND TARGET ascii-chat AND NOT EMSCRIPTEN AND CMAKE_BUILD_TYPE MATCHES "^(Debug|Dev)$")
    add_test(NAME client-connection-lifecycle
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/integration/client_connection_lifecycle.py
            $<TARGET_FILE:ascii-chat>)
    set_tests_properties(client-connection-lifecycle PROPERTIES LABELS "integration" TIMEOUT 300)
endif()

# Independent connection owners with real blocked TCP workers, without Criterion.
if(BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(test-session-connections standalone/session/connection_test.c standalone/session/protocol_test.c)
    target_link_libraries(test-session-connections PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    target_include_directories(test-session-connections PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    if(WIN32)
        target_link_libraries(test-session-connections PRIVATE ws2_32)
    endif()
    apply_sanitizers_to_target(test-session-connections)
    set_target_properties(test-session-connections PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    foreach(test_build_target tests tests-criterion-build tests-lib-build)
        if(TARGET ${test_build_target})
            add_dependencies(${test_build_target} test-session-connections)
        endif()
    endforeach()
    add_test(NAME session-connections COMMAND test-session-connections)
    set_tests_properties(session-connections PROPERTIES LABELS "integration" TIMEOUT 30)
endif()

if(BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(test-peer-ready standalone/network/peer_ready_test.c)
    if(WIN32)
        target_link_libraries(test-peer-ready PRIVATE ws2_32)
    endif()
    target_link_libraries(test-peer-ready PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    target_include_directories(test-peer-ready PRIVATE ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    apply_sanitizers_to_target(test-peer-ready)
    set_target_properties(test-peer-ready PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    foreach(test_build_target tests tests-criterion-build tests-lib-build)
        if(TARGET ${test_build_target})
            add_dependencies(${test_build_target} test-peer-ready)
        endif()
    endforeach()
    add_test(NAME peer-ready COMMAND test-peer-ready)
    set_tests_properties(peer-ready PROPERTIES LABELS "integration" TIMEOUT 30)
endif()

if(BUILD_TESTS AND WIN32)
    add_executable(test-terminal-resize standalone/session/terminal_resize_test.c)
    target_link_libraries(test-terminal-resize PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ws2_32)
    target_include_directories(test-terminal-resize PRIVATE ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    apply_sanitizers_to_target(test-terminal-resize)
    set_target_properties(test-terminal-resize PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(TARGET tests)
        add_dependencies(tests test-terminal-resize)
    endif()
    add_test(NAME terminal-resize COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/terminal_resize_windows.py $<TARGET_FILE:test-terminal-resize>)
    set_tests_properties(terminal-resize PROPERTIES LABELS "integration" TIMEOUT 40)
endif()

# Owned plaintext admission, real backend saturation, and codec recovery.
if(TARGET ascii-chat-static AND NOT EMSCRIPTEN)
    foreach(outbound_fixture backpressure backpressure-live)
        string(REPLACE "-" "_" outbound_source "${outbound_fixture}")
        if(outbound_fixture STREQUAL "backpressure")
            set(outbound_source backpressure_test)
        endif()
        add_executable(test-${outbound_fixture} EXCLUDE_FROM_ALL
            ${CMAKE_SOURCE_DIR}/tests/standalone/network/${outbound_source}.c)
        target_link_libraries(test-${outbound_fixture} PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
        if(WIN32)
            target_link_libraries(test-${outbound_fixture} PRIVATE ws2_32)
        endif()
        apply_sanitizers_to_target(test-${outbound_fixture})
        target_include_directories(test-${outbound_fixture} PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
        set_target_properties(test-${outbound_fixture} PROPERTIES LINKER_LANGUAGE CXX
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        foreach(test_build_target tests tests-criterion-build tests-lib-build)
            if(TARGET ${test_build_target})
                add_dependencies(${test_build_target} test-${outbound_fixture})
            endif()
        endforeach()
    endforeach()
    target_link_libraries(test-backpressure-live PRIVATE libdatachannel ${LIBWEBSOCKETS_LIBRARIES}
        ${LWS_OPENSSL_SSL_LIBRARY} ${LWS_OPENSSL_CRYPTO_LIBRARY} ${FFMPEG_LIBRARIES})
    if(APPLE)
        target_link_libraries(test-backpressure-live PRIVATE "-framework CoreFoundation" "-framework SystemConfiguration")
    endif()
    target_link_directories(test-backpressure-live PRIVATE ${FFMPEG_LIBRARY_DIRS})
    target_include_directories(test-backpressure-live PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIR} ${LWS_OPENSSL_PREFIX}/include ${FFMPEG_INCLUDE_DIRS})
    if(BUILD_TESTS)
        add_test(NAME transport-backpressure COMMAND test-backpressure)
        set_tests_properties(transport-backpressure PROPERTIES TIMEOUT 30)
        set(outbound_codecs raw,h264,h265)
        if(WIN32)
            # The LGPL FFmpeg bundle has no software HEVC encoder.
            set(outbound_codecs raw,h264)
        endif()
        foreach(outbound_transport tcp websocket webrtc)
            add_test(NAME backpressure-${outbound_transport} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/integration/transport_backpressure.py
                --binary $<TARGET_FILE:test-backpressure-live>
                --transports ${outbound_transport} --codecs ${outbound_codecs}
                --output ${CMAKE_BINARY_DIR}/backpressure/${outbound_transport})
            set_tests_properties(backpressure-${outbound_transport} PROPERTIES LABELS "integration" TIMEOUT 180 RUN_SERIAL TRUE)
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
                set_property(TEST backpressure-${outbound_transport} APPEND PROPERTY ENVIRONMENT_MODIFICATION
                    "LSAN_OPTIONS=string_append::suppressions=${CMAKE_CURRENT_SOURCE_DIR}/standalone/network/x265_3_5_lsan.supp")
            endif()
        endforeach()
        if(WIN32)
            set_tests_properties(transport-backpressure backpressure-tcp backpressure-websocket backpressure-webrtc
                PROPERTIES ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin;PATH=path_list_prepend:${ASCIICHAT_SANITIZER_RUNTIME_DIR}")
        endif()
    endif()
endif()

# Shared parser and production UI paste collector; no camera, clipboard mutation, or Criterion required.
if(BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(test-media-input standalone/media/input_test.c)
    target_link_libraries(test-media-input PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${LIBSODIUM_LIBRARIES})
    target_include_directories(test-media-input PRIVATE ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated)
    apply_sanitizers_to_target(test-media-input)
    set_target_properties(test-media-input PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    add_test(NAME media-input COMMAND test-media-input)
    set_tests_properties(media-input PROPERTIES LABELS "unit" TIMEOUT 30)
    if(TARGET tests)
        add_dependencies(tests test-media-input)
    endif()
endif()

if(BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(test-media-capture-open standalone/media/capture_open_test.c)
    target_link_libraries(test-media-capture-open PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${LIBSODIUM_LIBRARIES})
    target_include_directories(test-media-capture-open PRIVATE ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated)
    apply_sanitizers_to_target(test-media-capture-open)
    set_target_properties(test-media-capture-open PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    add_test(NAME media-capture-open COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/media_capture_open.py $<TARGET_FILE:test-media-capture-open>)
    set_tests_properties(media-capture-open PROPERTIES LABELS "integration" TIMEOUT 90 SKIP_RETURN_CODE 77)
    if(TARGET tests)
        add_dependencies(tests test-media-capture-open)
    endif()
endif()

if(BUILD_TESTS AND WIN32)
    add_executable(test-media-clipboard standalone/media/clipboard_windows_test.c)
    target_link_libraries(test-media-clipboard PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    target_include_directories(test-media-clipboard PRIVATE ${CMAKE_BINARY_DIR}/generated)
    apply_sanitizers_to_target(test-media-clipboard)
    set_target_properties(test-media-clipboard PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    add_test(NAME media-clipboard COMMAND test-media-clipboard)
    set_tests_properties(media-clipboard PROPERTIES LABELS "unit" TIMEOUT 30)
    if(TARGET tests)
        add_dependencies(tests test-media-clipboard)
    endif()
endif()

if(BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(test-media-pipeline standalone/media/pipeline_latest_test.c)
    target_link_libraries(test-media-pipeline PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${LIBSODIUM_LIBRARIES})
    target_include_directories(test-media-pipeline PRIVATE ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated)
    apply_sanitizers_to_target(test-media-pipeline)
    set_target_properties(test-media-pipeline PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    add_test(NAME media-pipeline COMMAND test-media-pipeline)
    set_tests_properties(media-pipeline PROPERTIES LABELS "unit" TIMEOUT 30)
    if(TARGET tests)
        add_dependencies(tests test-media-pipeline)
    endif()
endif()

if(BUILD_TESTS AND WIN32)
    add_executable(test-audio-priority standalone/media/audio_priority_test.c)
    target_link_libraries(test-audio-priority PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    target_include_directories(test-audio-priority PRIVATE ${CMAKE_BINARY_DIR}/generated)
    apply_sanitizers_to_target(test-audio-priority)
    set_target_properties(test-audio-priority PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    add_test(NAME audio-priority COMMAND test-audio-priority)
    set_tests_properties(audio-priority PROPERTIES LABELS "integration" TIMEOUT 90)
    if(TARGET tests)
        add_dependencies(tests test-audio-priority)
    endif()
endif()
