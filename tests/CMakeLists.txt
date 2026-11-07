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
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    # Keep every Python CTest harness on one execution path. Coverage builds
    # retain native counters beside their gcno metadata and expose the build
    # root to harnesses that verify specific profiles.
    function(asciichat_add_python_test name script)
        set(_coverage_args)
        if(ASCIICHAT_ENABLE_COVERAGE)
            list(APPEND _coverage_args --coverage-build --coverage-root ${CMAKE_BINARY_DIR})
        endif()
        add_test(NAME ${name} COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/scripts/run_python_test.py ${_coverage_args} --
            ${Python3_EXECUTABLE} ${script} ${ARGN})
    endfunction()

    # Pytest suites use a build-local uv environment so native and Docker builds
    # never replace one another's virtual environment. CTest reserves all cores
    # for each xdist batch rather than oversubscribing concurrent batches.
    find_program(ASCIICHAT_UV_EXECUTABLE uv REQUIRED)
    foreach(check IN ITEMS ruff ty)
        if(check STREQUAL "ruff")
            set(check_paths ${CMAKE_SOURCE_DIR}/tests)
        else()
            set(check_paths ${CMAKE_SOURCE_DIR}/tests/python ${CMAKE_SOURCE_DIR}/tests/ascii_chat_tests)
        endif()
        add_test(NAME python-${check} COMMAND ${CMAKE_COMMAND} -E env
            "UV_PROJECT_ENVIRONMENT=${CMAKE_BINARY_DIR}/python-test-venv"
            ${ASCIICHAT_UV_EXECUTABLE} run --project ${CMAKE_SOURCE_DIR}/tests/ascii_chat_tests
            --frozen --python ${Python3_EXECUTABLE} ${check} check ${check_paths})
        set_tests_properties(python-${check} PROPERTIES LABELS "unit;python;static" TIMEOUT 120)
    endforeach()
    function(asciichat_add_pytest name suite)
        set(_coverage_args)
        if(ASCIICHAT_ENABLE_COVERAGE)
            list(APPEND _coverage_args --coverage-build --coverage-root ${CMAKE_BINARY_DIR})
        endif()
        add_test(NAME ${name} COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/scripts/run_python_test.py ${_coverage_args} --
            ${CMAKE_COMMAND} -E env "UV_PROJECT_ENVIRONMENT=${CMAKE_BINARY_DIR}/python-test-venv"
            ${ASCIICHAT_UV_EXECUTABLE} run --project ${CMAKE_SOURCE_DIR}/tests/ascii_chat_tests
            --frozen --python ${Python3_EXECUTABLE} -m pytest
            ${CMAKE_SOURCE_DIR}/tests/python/${suite}
            --junitxml=${CMAKE_BINARY_DIR}/Testing/${name}.xml ${ARGN})
        set_tests_properties(${name} PROPERTIES PROCESSORS ${CPU_CORES} TIMEOUT 300)
    endfunction()
    asciichat_add_pytest(python-cmake cmake)
    set_tests_properties(python-cmake PROPERTIES LABELS "unit;python;build")
    asciichat_add_pytest(python-unit unit)
    set_tests_properties(python-unit PROPERTIES LABELS "unit;python")

    # These dependency-isolated build matrices compile selected production
    # sources directly. The shared wrapper retains their profiles under the
    # active build tree when coverage is enabled.
    set(standalone_includes --native-include=${CMAKE_BINARY_DIR}/generated)
    if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
        list(APPEND standalone_includes --native-include=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include)
    endif()
    asciichat_add_pytest(python-standalone standalone --c-compiler=${CMAKE_C_COMPILER} ${standalone_includes})
    set_tests_properties(python-standalone PROPERTIES LABELS "unit;python;standalone" TIMEOUT 300)

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

# Instrumentation slows tests and increases memory per Criterion worker.
if(IS_COVERAGE_BUILD OR ASCIICHAT_SANITIZER_COMPILE_FLAGS MATCHES "fsanitize=")
    set(CRITERION_TIMEOUT 60)      # Criterion's internal timeout per test case
    set(CTEST_TEST_TIMEOUT 120)    # CTest's overall timeout per test executable
    set(CRITERION_JOBS 1)          # Disable internal parallelism for coverage accuracy
    set(CTEST_PARALLEL_JOBS "${CPU_CORES}")
else()
    set(CRITERION_TIMEOUT 25)      # Default timeout
    set(CTEST_TEST_TIMEOUT 30)     # Tests must complete in 30 seconds or less
    # CTest distributes executables across cores; do not multiply Criterion workers.
    set(CRITERION_JOBS 1)
    set(CTEST_PARALLEL_JOBS "${CPU_CORES}")
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

# White-box contract suites retain explicit production link dependencies.
function(asciichat_configure_contract target)
    set_property(TARGET ${target} PROPERTY ASCIICHAT_KEEP_TEST_REGISTRATION TRUE)
    target_link_libraries(${target} PRIVATE ${CRITERION_LIBRARIES})
    target_link_directories(${target} PRIVATE ${CRITERION_LIBRARY_DIRS})
    target_include_directories(${target} PRIVATE ${CRITERION_INCLUDE_DIRS})
    set_target_properties(${target} PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF
        INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO OFF)
    # Registrations are compiled directly into these executables. Omitting the
    # shared dead-strip policy preserves them without force-loading production
    # archives, which would conflict with white-box replacement definitions.
    if(NOT APPLE)
        target_link_options(${target} PRIVATE -Wl,--no-gc-sections)
    endif()
    foreach(build_target tests tests-criterion-build test_all)
        if(TARGET ${build_target})
            add_dependencies(${build_target} ${target})
        endif()
    endforeach()
endfunction()

function(asciichat_add_contract_test name target)
    set_property(DIRECTORY APPEND PROPERTY ASCIICHAT_CONTRACT_TESTS ${name})
    add_test(NAME ${name} COMMAND ${target} --jobs 1 --timeout 60
        --xml=${CRITERION_XML_DIR}/${name}.xml)
    set_tests_properties(${name} PROPERTIES LABELS "criterion;contract"
        ENVIRONMENT "TESTING=1;CRITERION_TEST=1;BUILD_DIR=${CMAKE_BINARY_DIR}"
        TIMEOUT 180 RUN_SERIAL TRUE)
endfunction()

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

        if(test_exe_name STREQUAL "test_unit_log_json")
            target_include_directories(${test_exe_name} PRIVATE ${YYJSON_INCLUDE_DIRS})
            target_link_libraries(${test_exe_name} ${YYJSON_LIBRARIES})
        endif()

        if(USE_MIMALLOC)
            target_link_libraries(${test_exe_name} ${MIMALLOC_LIBRARIES})
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

        if(test_exe_name STREQUAL "test_unit_otel_otel")
            target_link_libraries(${test_exe_name} gotel::testing)
        endif()

        # The regex fixtures call PCRE2 directly to prepare and inspect native patterns.
        if(test_exe_name MATCHES "^test_unit_util_regex_(fallback|match)$")
            target_include_directories(${test_exe_name} PRIVATE ${PCRE2_INCLUDE_DIRS})
            target_link_libraries(${test_exe_name} ${PCRE2_LIBRARIES})
        endif()

        target_link_directories(${test_exe_name} PRIVATE ${FFMPEG_LIBRARY_DIRS})

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
        # Registrations are direct executable objects. The project policy keeps
        # them without force-loading unrelated archives and duplicate symbols.
        if(NOT WIN32 AND NOT APPLE)
            target_link_options(${test_exe_name} PRIVATE -Wl,--no-gc-sections)
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
    # CTest distributes test executables over all detected cores.
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
    add_test(NAME cmake-static-archive
        COMMAND "${CMAKE_COMMAND}" "-DCC=${CMAKE_C_COMPILER}" "-DAR=${CMAKE_AR}" "-DRANLIB=${CMAKE_RANLIB}"
            "-DSTATIC_ARCHIVE_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/static-archive"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/static_archive.cmake")
    set_tests_properties(cmake-static-archive PROPERTIES LABELS "unit" TIMEOUT 30)
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
    if(BUILD_CRITERION_TESTS AND CRITERION_FOUND)
        add_executable(test-source-paths EXCLUDE_FROM_ALL integration/source_paths.c)
        asciichat_configure_contract(test-source-paths)
        target_link_libraries(test-source-paths PRIVATE ascii-chat-shared)
        apply_sanitizers_to_target(test-source-paths)
        target_include_directories(test-source-paths PRIVATE ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
        set_target_properties(test-source-paths PROPERTIES
            LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        if(BUILD_TESTS)
            asciichat_add_contract_test(source-paths test-source-paths)
            set_tests_properties(source-paths PROPERTIES LABELS "integration" TIMEOUT 30)
            foreach(test_build_target tests tests-criterion-build tests-lib-build)
                if(TARGET ${test_build_target})
                    add_dependencies(${test_build_target} test-source-paths)
                endif()
            endforeach()
        endif()

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
        set_tests_properties(sync-regressions PROPERTIES LABELS "integration" TIMEOUT 60)
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
        set_tests_properties(shared-lib-runtime PROPERTIES LABELS "integration")
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

# Subprocess log and signal probes retain ordinary entry points.
if(TARGET ascii-chat-static)
    add_executable(test-crypto-logging EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/tests/standalone/logging/crypto_logging.c)
    target_link_libraries(test-crypto-logging PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    apply_sanitizers_to_target(test-crypto-logging)
    target_include_directories(test-crypto-logging PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
    set_target_properties(test-crypto-logging PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_TESTS)
        foreach(test_build_target tests tests-criterion-build tests-lib-build)
            if(TARGET ${test_build_target})
                add_dependencies(${test_build_target} test-crypto-logging)
            endif()
        endforeach()
        asciichat_add_pytest(python-crypto integration/crypto --crypto-logging-probe=$<TARGET_FILE:test-crypto-logging>)
        set_tests_properties(python-crypto PROPERTIES LABELS "integration;python")
        if(WIN32)
            set_property(TEST python-crypto PROPERTY ENVIRONMENT_MODIFICATION
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
        if(UNIX AND TARGET ascii-chat)
            set(errno_smoke_args --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                list(APPEND errno_smoke_args --debug-sync)
            endif()
            asciichat_add_pytest(python-errno-smoke integration/errno_smoke ${errno_smoke_args})
            set_tests_properties(python-errno-smoke PROPERTIES LABELS "integration;python;network" RUN_SERIAL TRUE)
        endif()
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND TARGET ascii-chat)
            asciichat_add_pytest(python-errno-signal integration/errno_signal --ascii-chat-binary=$<TARGET_FILE:ascii-chat>
                --errno-probe=$<TARGET_FILE:test-errno-stacks>)
            set_tests_properties(python-errno-signal PROPERTIES LABELS "integration;python;terminal" RUN_SERIAL TRUE)
        endif()
        # Contract cases are registered by the Criterion targets below.
    endif()

    add_executable(test-error-popup-fixture EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tests/standalone/errno/error_popup_fixture.c)
    target_link_libraries(test-error-popup-fixture PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    apply_sanitizers_to_target(test-error-popup-fixture)
    target_include_directories(test-error-popup-fixture PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
    set_target_properties(test-error-popup-fixture PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_TESTS)
        foreach(test_build_target tests tests-criterion-build tests-lib-build)
            if(TARGET ${test_build_target})
                add_dependencies(${test_build_target} test-error-popup-fixture)
            endif()
        endforeach()
        set(error_popup_args --error-popup-probe=$<TARGET_FILE:test-error-popup-fixture>)
        if(CMAKE_BUILD_TYPE MATCHES "^(Debug|Dev|RelWithDebInfo)$")
            list(APPEND error_popup_args --popup-diagnostics)
        endif()
        asciichat_add_pytest(python-error-popup integration/error_popup ${error_popup_args})
        set_tests_properties(python-error-popup PROPERTIES LABELS "integration;python;terminal;tui" RUN_SERIAL TRUE)
        if(WIN32)
            set_property(TEST python-error-popup PROPERTY ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        endif()
    endif()

    add_executable(test-errno-tui-fixture EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tests/standalone/errno/errno_tui_fixture.c)
    target_link_libraries(test-errno-tui-fixture PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    apply_sanitizers_to_target(test-errno-tui-fixture)
    target_include_directories(test-errno-tui-fixture PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
    set_target_properties(test-errno-tui-fixture PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_TESTS AND NOT WIN32 AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        foreach(test_build_target tests tests-criterion-build tests-lib-build)
            if(TARGET ${test_build_target})
                add_dependencies(${test_build_target} test-errno-tui-fixture)
            endif()
        endforeach()
        asciichat_add_pytest(python-errno-tui integration/errno_tui --errno-tui-probe=$<TARGET_FILE:test-errno-tui-fixture>)
        set_tests_properties(python-errno-tui PROPERTIES LABELS "integration;python;terminal" RUN_SERIAL TRUE)
    endif()
endif()

# Criterion buffer ownership, queue, budget, and concurrency contracts.
if(TARGET ascii-chat-static AND BUILD_CRITERION_TESTS AND CRITERION_FOUND)
    add_executable(test-buffer-ownership EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/tests/contracts/buffer/buffer_test.c)
    asciichat_configure_contract(test-buffer-ownership)
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
        asciichat_add_contract_test(buffer-ownership test-buffer-ownership)
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
            add_dependencies(${test_target} ui-native-probe test-errno-stacks)
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
    asciichat_add_pytest(python-dependency dependency ${_dependency_paths_args})
    set_tests_properties(python-dependency PROPERTIES LABELS "unit;python;build")
    add_executable(test-client-like-lifecycle integration/client_like_lifecycle.c)
    # Windows retains the standalone race probe because Criterion is unavailable.
    if(BUILD_CRITERION_TESTS AND CRITERION_FOUND)
        add_executable(test-splash-init contracts/tui/splash_init_test.c)
        asciichat_configure_contract(test-splash-init)
    else()
        add_executable(test-splash-init standalone/tui/splash_init.c)
    endif()
    if(TARGET ascii-chat-shared)
        target_link_libraries(test-splash-init PRIVATE ascii-chat-shared ${MIMALLOC_LIBRARIES})
    else()
        target_link_libraries(test-splash-init PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    endif()
    target_include_directories(test-splash-init PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    apply_sanitizers_to_target(test-splash-init)
    set_target_properties(test-splash-init PROPERTIES
        LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_CRITERION_TESTS AND CRITERION_FOUND)
        asciichat_add_contract_test(splash-init test-splash-init)
        set_tests_properties(splash-init PROPERTIES LABELS "criterion;contract;integration;thread")
    else()
        add_test(NAME splash-init COMMAND $<TARGET_FILE:test-splash-init>)
        set_tests_properties(splash-init PROPERTIES LABELS "integration;thread" TIMEOUT 30
            ENVIRONMENT "TESTING=1;ASCII_CHAT_OTEL_ENABLED_TELEMETRY=none")
    endif()
    if(TARGET ascii-chat-shared)
        target_link_libraries(test-client-like-lifecycle PRIVATE ascii-chat-shared ${MIMALLOC_LIBRARIES})
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
            add_dependencies(${test_build_target} test-client-like-lifecycle test-splash-init)
        endif()
    endforeach()
    asciichat_add_pytest(python-lifecycle integration/lifecycle --lifecycle-probe=$<TARGET_FILE:test-client-like-lifecycle>)
    set_tests_properties(python-lifecycle PROPERTIES LABELS "integration;python")
    add_executable(test-mdns-advertiser EXCLUDE_FROM_ALL
        ${CMAKE_CURRENT_SOURCE_DIR}/standalone/network/mdns_advertiser_fixture.c)
    target_link_libraries(test-mdns-advertiser PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    target_include_directories(test-mdns-advertiser PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
    apply_sanitizers_to_target(test-mdns-advertiser)
    set_target_properties(test-mdns-advertiser PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    foreach(test_build_target tests tests-criterion-build tests-lib-build)
        if(TARGET ${test_build_target})
            add_dependencies(${test_build_target} test-mdns-advertiser)
        endif()
    endforeach()
    asciichat_add_pytest(python-manual integration/manual --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-manual PROPERTIES LABELS "integration;python;manual" RUN_SERIAL TRUE)
    asciichat_add_pytest(python-ui-discovery integration/ui_discovery --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-ui-discovery PROPERTIES LABELS "integration;python;terminal;network" RUN_SERIAL TRUE)
    asciichat_add_pytest(python-mdns integration/mdns --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-mdns PROPERTIES LABELS "integration;python;terminal" RUN_SERIAL TRUE)
    asciichat_add_pytest(python-mdns-cross integration/mdns_cross --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-mdns-cross PROPERTIES LABELS "integration;python;terminal;network" RUN_SERIAL TRUE)
endif()

# Exercises the production discovery state machine without external services.
if(BUILD_CRITERION_TESTS AND CRITERION_FOUND)
    add_executable(test-discovery-session-process
        contracts/discovery/session_process_test.c
        ${CMAKE_SOURCE_DIR}/src/discovery-service/server.c
        ${CMAKE_SOURCE_DIR}/src/discovery-service/signaling.c)
    asciichat_configure_contract(test-discovery-session-process)
    target_link_libraries(test-discovery-session-process PRIVATE ${SQLITE3_LIBRARIES})
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
    asciichat_add_python_test(discovery-session-process
        ${CMAKE_SOURCE_DIR}/tests/scripts/run_discovery_criterion.py
        $<TARGET_FILE:test-discovery-session-process> --jobs 1 --timeout 30
        --xml=${CRITERION_XML_DIR}/discovery-session-process.xml)
    set_tests_properties(discovery-session-process PROPERTIES LABELS "integration;criterion;contract" TIMEOUT 120)
endif()

if(BUILD_TESTS AND TARGET ascii-chat)
    asciichat_add_pytest(python-media integration/media --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-media PROPERTIES LABELS "integration;python;media")
    asciichat_add_pytest(python-discovery integration/discovery --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-discovery PROPERTIES LABELS "integration;python;network" RUN_SERIAL TRUE)
    foreach(test_build_target tests tests-criterion-build tests-lib-build)
        if(TARGET ${test_build_target})
            add_dependencies(${test_build_target} ascii-chat)
        endif()
    endforeach()
    asciichat_add_pytest(python-network integration/network --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-network PROPERTIES LABELS "integration;python;network")
    asciichat_add_pytest(python-recording integration/recording --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-recording PROPERTIES LABELS "integration;python;media" RUN_SERIAL TRUE)
endif()

# Production transport tests run natively on Windows, macOS, and Linux without Criterion.
if(BUILD_TESTS AND TARGET ascii-chat AND NOT EMSCRIPTEN)
    asciichat_add_pytest(python-event-io integration/event_io --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    # The admission stress fixture opens 90 peers and performs serial crypto
    # handshakes. Give it the same isolation as the Criterion process fixtures.
    set_tests_properties(python-event-io PROPERTIES LABELS "integration;python;network" RUN_SERIAL TRUE)
endif()

if(TARGET ascii-chat-static AND NOT EMSCRIPTEN)
    if(BUILD_CRITERION_TESTS AND CRITERION_FOUND)
        add_executable(test-webrtc-security EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/tests/contracts/network/webrtc_security_test.c)
        asciichat_configure_contract(test-webrtc-security)
        target_link_libraries(test-webrtc-security PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${LIBSODIUM_LIBRARIES})
        if(WIN32)
            target_link_libraries(test-webrtc-security PRIVATE ws2_32)
        endif()
        apply_sanitizers_to_target(test-webrtc-security)
        target_include_directories(test-webrtc-security PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
        set_target_properties(test-webrtc-security PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        if(BUILD_TESTS)
            asciichat_add_contract_test(webrtc-security test-webrtc-security)
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
        # Contract cases are registered by the Criterion targets below.
        asciichat_add_pytest(python-acip integration/acip --acip-websocket-probe=$<TARGET_FILE:test-acip-prepare>)
        set_tests_properties(python-acip PROPERTIES LABELS "integration;python;network")
        if(WIN32)
            set_property(TEST python-acip PROPERTY ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
                "PATH=path_list_prepend:${ASCIICHAT_SANITIZER_RUNTIME_DIR}")
        endif()
        if(TARGET tests)
            add_dependencies(tests test-acip-prepare)
        endif()
    endif()
    if(BUILD_CRITERION_TESTS AND CRITERION_FOUND)
        add_executable(test-network-events EXCLUDE_FROM_ALL
            ${CMAKE_SOURCE_DIR}/tests/contracts/network/event_test.c
            ${CMAKE_SOURCE_DIR}/tests/contracts/network/accepted_start_test.c)
        asciichat_configure_contract(test-network-events)
        target_link_libraries(test-network-events PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${ASCIICHAT_LIBUV_TARGET})
        apply_sanitizers_to_target(test-network-events)
        target_include_directories(test-network-events PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
        set_target_properties(test-network-events PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        if(BUILD_TESTS)
            asciichat_add_contract_test(network-events test-network-events)
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
        add_executable(test-network-connections EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/tests/contracts/network/connection_test.c)
        asciichat_configure_contract(test-network-connections)
        target_link_libraries(test-network-connections PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${ASCIICHAT_LIBUV_TARGET})
        apply_sanitizers_to_target(test-network-connections)
        target_include_directories(test-network-connections PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
        set_target_properties(test-network-connections PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        if(BUILD_TESTS)
            asciichat_add_contract_test(network-connections test-network-connections)
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
        # Contract cases are registered by the Criterion targets below.
    endif()
endif()

if(TARGET ascii-chat-static AND BUILD_CRITERION_TESTS AND CRITERION_FOUND)
    add_executable(test-crypto-policy EXCLUDE_FROM_ALL contracts/crypto/policy_test.c)
    asciichat_configure_contract(test-crypto-policy)
    target_link_libraries(test-crypto-policy PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    target_include_directories(test-crypto-policy PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
    apply_sanitizers_to_target(test-crypto-policy)
    set_target_properties(test-crypto-policy PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_TESTS)
        asciichat_add_contract_test(crypto-policy test-crypto-policy)
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

# Exercise the event-driven loop with real conversion and captured terminal output.
if(BUILD_TESTS AND TARGET ascii-chat-static AND NOT EMSCRIPTEN AND BUILD_CRITERION_TESTS AND CRITERION_FOUND)
    add_executable(test-render-loop contracts/session/render_loop_test.c)
    asciichat_configure_contract(test-render-loop)
    # The white-box copy defines render globals; use one static copy rather than
    # also loading those globals from the Debug shared library under ASan.
    target_link_libraries(test-render-loop PRIVATE ascii-chat-static-lib-with-deps ${MIMALLOC_LIBRARIES})
    target_include_directories(test-render-loop PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    apply_sanitizers_to_target(test-render-loop)
    set_target_properties(test-render-loop PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(render-loop test-render-loop)
    set_tests_properties(render-loop PROPERTIES LABELS "unit" TIMEOUT 30)
endif()
# White-box discovery NAT refresh checks mock only worker/network boundaries.
if(BUILD_TESTS AND NOT EMSCRIPTEN AND BUILD_CRITERION_TESTS AND CRITERION_FOUND)
    add_executable(test-nat-refresh contracts/discovery/nat_refresh_test.c)
    asciichat_configure_contract(test-nat-refresh)
    target_link_libraries(test-nat-refresh PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${LIBSODIUM_LIBRARIES})
    target_include_directories(test-nat-refresh PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/common)
    apply_sanitizers_to_target(test-nat-refresh)
    set_target_properties(test-nat-refresh PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    add_test(NAME discovery-nat-refresh COMMAND test-nat-refresh --jobs 1 --timeout 30
        --xml=${CRITERION_XML_DIR}/discovery-nat-refresh.xml)
    set_tests_properties(discovery-nat-refresh PROPERTIES LABELS "unit;criterion;contract" TIMEOUT 60
        ENVIRONMENT "TESTING=1;CRITERION_TEST=1;BUILD_DIR=${CMAKE_BINARY_DIR}")
endif()

# Shared discovery host rendering, including builds without Criterion.
if(TARGET ascii-chat-static AND NOT EMSCRIPTEN AND BUILD_CRITERION_TESTS AND CRITERION_FOUND)
    add_executable(test-host-render EXCLUDE_FROM_ALL contracts/session/host_render_test.c)
    asciichat_configure_contract(test-host-render)
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
        asciichat_add_contract_test(host-render test-host-render)
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
if(BUILD_TESTS AND NOT EMSCRIPTEN AND BUILD_CRITERION_TESTS AND CRITERION_FOUND)
    add_executable(test-review-findings contracts/review_findings_test.c)
    asciichat_configure_contract(test-review-findings)
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
    asciichat_add_contract_test(review-findings test-review-findings)
    set_tests_properties(review-findings PROPERTIES LABELS "unit" TIMEOUT 60)
    add_executable(test-runtime-settings contracts/options/runtime_test.c)
    asciichat_configure_contract(test-runtime-settings)
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
    asciichat_add_contract_test(runtime-settings test-runtime-settings)
    set_tests_properties(runtime-settings PROPERTIES LABELS "unit" TIMEOUT 60)
endif()

if(BUILD_TESTS AND BUILD_CRITERION_TESTS AND CRITERION_FOUND)
    add_executable(test-participant-media contracts/session/participant_media_test.c)
    asciichat_configure_contract(test-participant-media)
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
    asciichat_add_contract_test(participant-media test-participant-media)
    set_tests_properties(participant-media PROPERTIES LABELS "integration" TIMEOUT 60)
endif()

if(BUILD_TESTS AND TARGET ascii-chat AND NOT EMSCRIPTEN AND CMAKE_BUILD_TYPE MATCHES "^(Debug|Dev)$")
    asciichat_add_pytest(python-client-connection integration/client_connection --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-client-connection PROPERTIES LABELS "integration;python;network")
endif()

if(BUILD_TESTS AND TARGET ascii-chat AND NOT EMSCRIPTEN)
    asciichat_add_pytest(python-remote-errors integration/remote_errors --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-remote-errors PROPERTIES LABELS "integration;python;tui;network" RUN_SERIAL TRUE)
    if(WIN32)
        set_property(TEST python-remote-errors PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
            "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    endif()
endif()

# Server handlers linked with their real media pipeline and fixture-owned application state.
if(BUILD_TESTS AND NOT EMSCRIPTEN AND BUILD_CRITERION_TESTS AND CRITERION_FOUND)
    add_executable(test-signaling contracts/network/signaling_test.c
        ${CMAKE_SOURCE_DIR}/src/client/webrtc.c)
    asciichat_configure_contract(test-signaling)
    target_link_libraries(test-signaling PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    target_include_directories(test-signaling PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    apply_sanitizers_to_target(test-signaling)
    set_target_properties(test-signaling PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(signaling test-signaling)
    set_property(TEST signaling APPEND PROPERTY LABELS unit)

    add_executable(test-client-rekey contracts/crypto/client_rekey_test.c)
    asciichat_configure_contract(test-client-rekey)
    target_link_libraries(test-client-rekey PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${LIBSODIUM_LIBRARIES})
    target_include_directories(test-client-rekey PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    apply_sanitizers_to_target(test-client-rekey)
    set_target_properties(test-client-rekey PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(client-rekey test-client-rekey)
    set_property(TEST client-rekey APPEND PROPERTY LABELS unit)

    add_executable(test-server-stream contracts/network/server_stream_test.c
        ${CMAKE_SOURCE_DIR}/src/server/stream.c)
    asciichat_configure_contract(test-server-stream)
    target_link_libraries(test-server-stream PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    target_include_directories(test-server-stream PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    apply_sanitizers_to_target(test-server-stream)
    set_target_properties(test-server-stream PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(server-stream test-server-stream)
    set_property(TEST server-stream APPEND PROPERTY LABELS unit)

    add_executable(test-client-transport contracts/network/client_transport_test.c
        ${CMAKE_SOURCE_DIR}/src/client/server.c ${CMAKE_SOURCE_DIR}/src/client/keepalive.c
        ${CMAKE_SOURCE_DIR}/src/client/crypto.c ${CMAKE_SOURCE_DIR}/src/client/display.c)
    asciichat_configure_contract(test-client-transport)
    target_link_libraries(test-client-transport PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${FFMPEG_LIBRARIES})
    target_include_directories(test-client-transport PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    apply_sanitizers_to_target(test-client-transport)
    set_target_properties(test-client-transport PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(client-transport test-client-transport)
    set_property(TEST client-transport APPEND PROPERTY LABELS unit)

    add_executable(test-session-h265-client contracts/session/h265_client_test.c
        ${CMAKE_SOURCE_DIR}/src/session/h265/client.c)
    asciichat_configure_contract(test-session-h265-client)
    target_link_libraries(test-session-h265-client PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    target_include_directories(test-session-h265-client PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    apply_sanitizers_to_target(test-session-h265-client)
    set_target_properties(test-session-h265-client PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(session-h265-client test-session-h265-client)
    set_property(TEST session-h265-client APPEND PROPERTY LABELS unit)

    if(TARGET test_unit_session_test_api_boundaries)
        target_include_directories(test_unit_session_test_api_boundaries PRIVATE ${CMAKE_SOURCE_DIR}/src/common)
    endif()

    add_executable(test-server-lifecycle contracts/session/server_lifecycle_test.c)
    asciichat_configure_contract(test-server-lifecycle)
    target_link_libraries(test-server-lifecycle PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${FFMPEG_LIBRARIES})
    target_include_directories(test-server-lifecycle PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include} ${FFMPEG_INCLUDE_DIRS})
    apply_sanitizers_to_target(test-server-lifecycle)
    set_target_properties(test-server-lifecycle PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(shared-server-lifecycle test-server-lifecycle)
    set_property(TEST shared-server-lifecycle APPEND PROPERTY LABELS unit)


    add_executable(test-client-lifecycle contracts/session/client_lifecycle_test.c)
    asciichat_configure_contract(test-client-lifecycle)
    target_link_libraries(test-client-lifecycle PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${FFMPEG_LIBRARIES})
    target_include_directories(test-client-lifecycle PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include} ${FFMPEG_INCLUDE_DIRS})
    apply_sanitizers_to_target(test-client-lifecycle)
    set_target_properties(test-client-lifecycle PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(client-lifecycle test-client-lifecycle)
    set_property(TEST client-lifecycle APPEND PROPERTY LABELS unit)

    add_executable(test-mcp-requests contracts/mcp/request_contract_test.c)
    asciichat_configure_contract(test-mcp-requests)
    target_link_libraries(test-mcp-requests PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES}
        ${YYJSON_LIBRARIES} ${ASCIICHAT_LIBUV_TARGET} ${LIBWEBSOCKETS_LIBRARIES} ${LIBSODIUM_LIBRARIES})
    target_include_directories(test-mcp-requests PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include}
        ${YYJSON_INCLUDE_DIRS} ${LIBWEBSOCKETS_INCLUDE_DIRS})
    apply_sanitizers_to_target(test-mcp-requests)
    set_target_properties(test-mcp-requests PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(mcp-requests test-mcp-requests)
    set_property(TEST mcp-requests APPEND PROPERTY LABELS unit)

    add_executable(test-discovery-media contracts/discovery/media_callbacks_test.c)
    asciichat_configure_contract(test-discovery-media)
    target_link_libraries(test-discovery-media PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${FFMPEG_LIBRARIES})
    target_include_directories(test-discovery-media PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include} ${FFMPEG_INCLUDE_DIRS})
    apply_sanitizers_to_target(test-discovery-media)
    set_target_properties(test-discovery-media PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(discovery-media test-discovery-media)
    set_property(TEST discovery-media APPEND PROPERTY LABELS unit)

    add_executable(test-entry-signals contracts/entry/signals_test.c)
    asciichat_configure_contract(test-entry-signals)
    target_link_libraries(test-entry-signals PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${FFMPEG_LIBRARIES} ${YYJSON_LIBRARIES})
    target_include_directories(test-entry-signals PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include} ${FFMPEG_INCLUDE_DIRS} ${YYJSON_INCLUDE_DIRS} ${PCRE2_INCLUDE_DIRS})
    apply_sanitizers_to_target(test-entry-signals)
    set_target_properties(test-entry-signals PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(entry-signals test-entry-signals)
    set_property(TEST entry-signals APPEND PROPERTY LABELS unit)

    add_executable(test-participant-workers contracts/session/participant_workers_test.c)
    asciichat_configure_contract(test-participant-workers)
    target_link_libraries(test-participant-workers PRIVATE ascii-chat-static-lib-with-deps ${MIMALLOC_LIBRARIES})
    target_include_directories(test-participant-workers PRIVATE ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include} ${LIBSODIUM_INCLUDE_DIRS})
    apply_sanitizers_to_target(test-participant-workers)
    set_target_properties(test-participant-workers PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(participant-workers test-participant-workers)
    set_property(TEST participant-workers APPEND PROPERTY LABELS unit)

    add_executable(test-host-faults contracts/session/host_faults_test.c)
    asciichat_configure_contract(test-host-faults)
    target_link_libraries(test-host-faults PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${FFMPEG_LIBRARIES})
    target_include_directories(test-host-faults PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include} ${FFMPEG_INCLUDE_DIRS})
    apply_sanitizers_to_target(test-host-faults)
    set_target_properties(test-host-faults PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(host-faults test-host-faults)
    set_property(TEST host-faults APPEND PROPERTY LABELS unit)

    add_executable(test-client-video contracts/network/client_video_test.c
        ${CMAKE_SOURCE_DIR}/src/client/server.c ${CMAKE_SOURCE_DIR}/src/client/crypto.c
        ${CMAKE_SOURCE_DIR}/src/client/keepalive.c)
    asciichat_configure_contract(test-client-video)
    target_link_libraries(test-client-video PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${FFMPEG_LIBRARIES})
    target_include_directories(test-client-video PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include} ${FFMPEG_INCLUDE_DIRS})
    apply_sanitizers_to_target(test-client-video)
    set_target_properties(test-client-video PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(client-video test-client-video)
    set_property(TEST client-video APPEND PROPERTY LABELS unit)

    add_executable(test-client-audio contracts/network/client_audio_test.c
        ${CMAKE_SOURCE_DIR}/src/client/server.c
        ${CMAKE_SOURCE_DIR}/src/client/crypto.c
        ${CMAKE_SOURCE_DIR}/src/client/display.c
        ${CMAKE_SOURCE_DIR}/src/client/keepalive.c)
    asciichat_configure_contract(test-client-audio)
    target_link_libraries(test-client-audio PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${FFMPEG_LIBRARIES})
    target_include_directories(test-client-audio PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include} ${FFMPEG_INCLUDE_DIRS})
    apply_sanitizers_to_target(test-client-audio)
    set_target_properties(test-client-audio PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(client-audio test-client-audio)
    set_property(TEST client-audio APPEND PROPERTY LABELS unit)

    add_executable(test-capture-transactions contracts/media/capture_transactions_test.c)
    asciichat_configure_contract(test-capture-transactions)
    target_link_libraries(test-capture-transactions PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${FFMPEG_LIBRARIES})
    target_include_directories(test-capture-transactions PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include} ${FFMPEG_INCLUDE_DIRS})
    apply_sanitizers_to_target(test-capture-transactions)
    set_target_properties(test-capture-transactions PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(capture-transactions test-capture-transactions)
    set_property(TEST capture-transactions APPEND PROPERTY LABELS unit)

    add_executable(test-display-delivery contracts/media/display_delivery_test.c)
    asciichat_configure_contract(test-display-delivery)
    target_link_libraries(test-display-delivery PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${FFMPEG_LIBRARIES})
    target_include_directories(test-display-delivery PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include} ${FFMPEG_INCLUDE_DIRS})
    apply_sanitizers_to_target(test-display-delivery)
    set_target_properties(test-display-delivery PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(display-delivery test-display-delivery)
    set_property(TEST display-delivery APPEND PROPERTY LABELS unit)

    add_executable(test-discovery-service-callbacks contracts/discovery/service_callbacks_test.c
        ${CMAKE_SOURCE_DIR}/src/discovery-service/signaling.c)
    asciichat_configure_contract(test-discovery-service-callbacks)
    target_link_libraries(test-discovery-service-callbacks PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${SQLITE3_LIBRARIES} ${LIBSODIUM_LIBRARIES})
    target_include_directories(test-discovery-service-callbacks PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    apply_sanitizers_to_target(test-discovery-service-callbacks)
    set_target_properties(test-discovery-service-callbacks PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(discovery-service-callbacks test-discovery-service-callbacks)
    set_property(TEST discovery-service-callbacks APPEND PROPERTY LABELS unit)

    add_executable(test-server-main contracts/network/server_main_test.c
        ${CMAKE_SOURCE_DIR}/src/server/client.c ${CMAKE_SOURCE_DIR}/src/server/connections.c
        ${CMAKE_SOURCE_DIR}/src/server/protocol.c ${CMAKE_SOURCE_DIR}/src/server/stream.c
        ${CMAKE_SOURCE_DIR}/src/server/render.c ${CMAKE_SOURCE_DIR}/src/server/stats.c
        ${CMAKE_SOURCE_DIR}/src/session/h265/server.c)
    asciichat_configure_contract(test-server-main)
    target_link_libraries(test-server-main PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${FFMPEG_LIBRARIES})
    target_include_directories(test-server-main PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include} ${FFMPEG_INCLUDE_DIRS})
    apply_sanitizers_to_target(test-server-main)
    set_target_properties(test-server-main PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(server-main test-server-main)
    set_property(TEST server-main APPEND PROPERTY LABELS unit)

    add_executable(test-server-render contracts/network/server_render_test.c)
    asciichat_configure_contract(test-server-render)
    target_link_libraries(test-server-render PRIVATE ascii-chat-static-lib-with-deps ${MIMALLOC_LIBRARIES})
    target_include_directories(test-server-render PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    apply_sanitizers_to_target(test-server-render)
    set_target_properties(test-server-render PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(server-render test-server-render)
    set_property(TEST server-render APPEND PROPERTY LABELS unit)

    add_executable(test-server-client contracts/network/server_client_test.c
        ${CMAKE_SOURCE_DIR}/src/server/protocol.c
        ${CMAKE_SOURCE_DIR}/src/session/h265/server.c)
    asciichat_configure_contract(test-server-client)
    target_link_libraries(test-server-client PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${FFMPEG_LIBRARIES})
    target_include_directories(test-server-client PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include} ${FFMPEG_INCLUDE_DIRS})
    apply_sanitizers_to_target(test-server-client)
    set_target_properties(test-server-client PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(server-client test-server-client)
    set_property(TEST server-client APPEND PROPERTY LABELS unit)

    add_executable(test-server-protocol contracts/network/server_protocol_test.c
        ${CMAKE_SOURCE_DIR}/src/server/protocol.c ${CMAKE_SOURCE_DIR}/src/session/h265/server.c)
    asciichat_configure_contract(test-server-protocol)
    target_link_libraries(test-server-protocol PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    target_include_directories(test-server-protocol PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    apply_sanitizers_to_target(test-server-protocol)
    set_target_properties(test-server-protocol PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(server-protocol test-server-protocol)
    set_tests_properties(server-protocol PROPERTIES LABELS "unit" TIMEOUT 30)
endif()

# Audio device selection and callbacks with a deterministic PortAudio boundary.
if(BUILD_TESTS AND NOT EMSCRIPTEN AND BUILD_CRITERION_TESTS AND CRITERION_FOUND)
    add_executable(test-audio-device contracts/audio/device_test.c)
    asciichat_configure_contract(test-audio-device)
    # Bind device calls to the executable's deterministic PortAudio boundary.
    # Mach-O's two-level namespace cannot interpose calls from our shared library.
    target_link_libraries(test-audio-device PRIVATE ascii-chat-static-lib-with-deps ${MIMALLOC_LIBRARIES})
    add_dependencies(test-audio-device build-ascii-chat-static-lib)
    target_include_directories(test-audio-device PRIVATE
        ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include} ${PORTAUDIO_INCLUDE_DIRS})
    apply_sanitizers_to_target(test-audio-device)
    set_target_properties(test-audio-device PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(audio-device test-audio-device)
    set_tests_properties(audio-device PROPERTIES LABELS "unit" TIMEOUT 30)
endif()

# Discovery host ranking and local wire probes use the production mode implementation.
if(BUILD_TESTS AND NOT EMSCRIPTEN AND BUILD_CRITERION_TESTS AND CRITERION_FOUND)
    add_executable(test-nat-quality contracts/discovery/nat_quality_test.c
        ${CMAKE_SOURCE_DIR}/src/discovery/nat.c)
    asciichat_configure_contract(test-nat-quality)
    target_link_libraries(test-nat-quality PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    target_include_directories(test-nat-quality PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    apply_sanitizers_to_target(test-nat-quality)
    set_target_properties(test-nat-quality PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(nat-quality test-nat-quality)
    set_tests_properties(nat-quality PROPERTIES LABELS "unit" TIMEOUT 30)
endif()

# Criterion connection owner tests with real blocked TCP workers.
if(BUILD_TESTS AND NOT EMSCRIPTEN AND BUILD_CRITERION_TESTS AND CRITERION_FOUND)
    add_executable(test-session-connections contracts/session/connection_test.c contracts/session/protocol_test.c
        contracts/session/establish_test.c)
    asciichat_configure_contract(test-session-connections)
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
    asciichat_add_contract_test(session-connections test-session-connections)
    set_tests_properties(session-connections PROPERTIES LABELS "integration" TIMEOUT 30)
endif()

if(BUILD_TESTS AND NOT EMSCRIPTEN AND BUILD_CRITERION_TESTS AND CRITERION_FOUND)
    add_executable(test-peer-ready contracts/network/peer_ready_test.c)
    asciichat_configure_contract(test-peer-ready)
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
    asciichat_add_contract_test(peer-ready test-peer-ready)
    set_tests_properties(peer-ready PROPERTIES LABELS "integration" TIMEOUT 30)
endif()

if(BUILD_TESTS AND NOT EMSCRIPTEN AND TARGET ascii-chat-static)
    add_executable(test-terminal-pty standalone/platform/terminal_pty_test.c)
    target_link_libraries(test-terminal-pty PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    target_include_directories(test-terminal-pty PRIVATE ${CMAKE_BINARY_DIR}/generated ${_asciichat_mimalloc_include})
    apply_sanitizers_to_target(test-terminal-pty)
    set_target_properties(test-terminal-pty PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    foreach(test_build_target tests tests-criterion-build test_all)
        if(TARGET ${test_build_target})
            add_dependencies(${test_build_target} test-terminal-pty)
        endif()
    endforeach()
    asciichat_add_pytest(python-terminal integration/terminal --terminal-probe=$<TARGET_FILE:test-terminal-pty>)
    set_tests_properties(python-terminal PROPERTIES LABELS "integration;python;tui;terminal")
    if(UNIX)
        add_test(NAME terminal-pty COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/integration/terminal_pty.py $<TARGET_FILE:test-terminal-pty>)
        set_tests_properties(terminal-pty PROPERTIES LABELS "integration" TIMEOUT 45)
    endif()
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
    asciichat_add_pytest(python-resize integration/resize --terminal-resize-probe=$<TARGET_FILE:test-terminal-resize>)
    set_tests_properties(python-resize PROPERTIES LABELS "integration;python;terminal")
endif()

# Owned plaintext admission, real backend saturation, and codec recovery.
if(TARGET ascii-chat-static AND NOT EMSCRIPTEN)
    foreach(outbound_fixture backpressure backpressure-live)
        if(outbound_fixture STREQUAL "backpressure" AND NOT (BUILD_CRITERION_TESTS AND CRITERION_FOUND))
            continue()
        endif()
        string(REPLACE "-" "_" outbound_source "${outbound_fixture}")
        if(outbound_fixture STREQUAL "backpressure")
            set(outbound_source backpressure_test)
            set(outbound_directory contracts)
        else()
            set(outbound_directory standalone)
        endif()
        add_executable(test-${outbound_fixture} EXCLUDE_FROM_ALL
            ${CMAKE_SOURCE_DIR}/tests/${outbound_directory}/network/${outbound_source}.c)
        if(outbound_fixture STREQUAL "backpressure")
            asciichat_configure_contract(test-backpressure)
        endif()
        target_link_libraries(test-${outbound_fixture} PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
        if(WIN32)
            if(outbound_fixture STREQUAL "backpressure")
            asciichat_configure_contract(test-backpressure)
        endif()
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
        if(TARGET test-backpressure)
            asciichat_add_contract_test(transport-backpressure test-backpressure)
        endif()
        set(outbound_codecs raw,h264,h265)
        if(WIN32)
            # The LGPL FFmpeg bundle has no software HEVC encoder.
            set(outbound_codecs raw,h264)
        endif()
        foreach(outbound_transport tcp websocket webrtc)
            asciichat_add_pytest(python-backpressure-${outbound_transport} integration/backpressure
                --backpressure-probe=$<TARGET_FILE:test-backpressure-live>
                --backpressure-transports=${outbound_transport} --backpressure-codecs=${outbound_codecs}
                --backpressure-output=${CMAKE_BINARY_DIR}/backpressure/${outbound_transport})
            set_tests_properties(python-backpressure-${outbound_transport} PROPERTIES LABELS "integration;python;network" RUN_SERIAL TRUE)
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
                set_property(TEST python-backpressure-${outbound_transport} APPEND PROPERTY ENVIRONMENT_MODIFICATION
                    "LSAN_OPTIONS=string_append::suppressions=${CMAKE_CURRENT_SOURCE_DIR}/standalone/network/x265_3_5_lsan.supp")
            endif()
        endforeach()
        if(WIN32)
            set_tests_properties(python-backpressure-tcp python-backpressure-websocket python-backpressure-webrtc
                PROPERTIES ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin;PATH=path_list_prepend:${ASCIICHAT_SANITIZER_RUNTIME_DIR}")
        endif()
    endif()
endif()

# Criterion parser and UI paste collector tests; no camera or clipboard mutation.
if(BUILD_TESTS AND NOT EMSCRIPTEN AND BUILD_CRITERION_TESTS AND CRITERION_FOUND)
    add_executable(test-media-input contracts/media/input_test.c)
    asciichat_configure_contract(test-media-input)
    target_link_libraries(test-media-input PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${LIBSODIUM_LIBRARIES})
    target_include_directories(test-media-input PRIVATE ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated)
    apply_sanitizers_to_target(test-media-input)
    set_target_properties(test-media-input PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(media-input test-media-input)
    set_tests_properties(media-input PROPERTIES LABELS "unit" TIMEOUT 30)
    if(TARGET tests)
        add_dependencies(tests test-media-input)
    endif()
endif()

if(BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(test-media-capture-pacing standalone/media/capture_pacing_test.c)
    target_include_directories(test-media-capture-pacing PRIVATE ${CMAKE_SOURCE_DIR}/src/common)
    add_test(NAME media-capture-pacing COMMAND test-media-capture-pacing)
    set_tests_properties(media-capture-pacing PROPERTIES LABELS "unit;media" TIMEOUT 10)
    if(TARGET tests)
        add_dependencies(tests test-media-capture-pacing)
    endif()
endif()

if(BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(test-media-capture-open standalone/media/capture_open_test.c)
    target_link_libraries(test-media-capture-open PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${LIBSODIUM_LIBRARIES})
    target_include_directories(test-media-capture-open PRIVATE ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated)
    apply_sanitizers_to_target(test-media-capture-open)
    set_target_properties(test-media-capture-open PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_pytest(python-capture integration/capture --media-capture-probe=$<TARGET_FILE:test-media-capture-open>)
    set_tests_properties(python-capture PROPERTIES LABELS "integration;python;media")
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

if(BUILD_TESTS AND NOT EMSCRIPTEN AND BUILD_CRITERION_TESTS AND CRITERION_FOUND)
    add_executable(test-media-pipeline contracts/media/pipeline_latest_test.c)
    asciichat_configure_contract(test-media-pipeline)
    target_link_libraries(test-media-pipeline PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${LIBSODIUM_LIBRARIES})
    target_include_directories(test-media-pipeline PRIVATE ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated)
    apply_sanitizers_to_target(test-media-pipeline)
    set_target_properties(test-media-pipeline PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_contract_test(media-pipeline test-media-pipeline)
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

if(BUILD_CRITERION_TESTS AND CRITERION_FOUND AND TARGET ascii-chat-static)
    foreach(contract errno prepare vectored)
        if(contract STREQUAL "errno")
            set(contract_dir errno)
            set(contract_name errno-stacks)
        elseif(contract STREQUAL "prepare")
            set(contract_dir network)
            set(contract_name acip-prepare)
        else()
            set(contract_dir network)
            set(contract_name network-vectors)
        endif()
        set(contract_target test-contract-${contract})
        add_executable(${contract_target} contracts/${contract_dir}/${contract}_test.c)
        target_link_libraries(${contract_target} PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${ASCIICHAT_LIBUV_TARGET})
        target_include_directories(${contract_target} PRIVATE ${CMAKE_BINARY_DIR}/generated)
        apply_sanitizers_to_target(${contract_target})
        set_target_properties(${contract_target} PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        asciichat_configure_contract(${contract_target})
        asciichat_add_contract_test(${contract_name} ${contract_target})
    endforeach()
endif()

if(BUILD_TESTS AND TARGET ascii-chat)
    # The Criterion client-transport contract covers this on supported Unix platforms.
    # Keep the standalone variant on Windows, where Criterion is unavailable.
    if(NOT BUILD_CRITERION_TESTS OR NOT CRITERION_FOUND)
    add_executable(test-client-ping-sentinel standalone/network/client_ping_test.c)
    target_link_libraries(test-client-ping-sentinel PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    target_include_directories(test-client-ping-sentinel PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated)
    apply_sanitizers_to_target(test-client-ping-sentinel)
    set_target_properties(test-client-ping-sentinel PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    add_test(NAME client-ping-sentinel COMMAND test-client-ping-sentinel)
    set_tests_properties(client-ping-sentinel PROPERTIES LABELS "unit;network" TIMEOUT 30)
    endif()
    add_executable(test-yt-dlp-fixture standalone/media/fake_yt_dlp.c)
    set_target_properties(test-yt-dlp-fixture PROPERTIES OUTPUT_NAME fake-yt-dlp
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-fixtures")
    add_executable(test-extractor-audio standalone/media/extractor_audio_probe.c standalone/media/extractor_cpp_probe.cpp)
    target_link_libraries(test-extractor-audio PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES})
    target_include_directories(test-extractor-audio PRIVATE ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated)
    apply_sanitizers_to_target(test-extractor-audio)
    set_target_properties(test-extractor-audio PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_add_pytest(python-extractor integration/extractor --ascii-chat-binary=$<TARGET_FILE:ascii-chat>
        --yt-dlp-probe=$<TARGET_FILE:test-yt-dlp-fixture> --extractor-audio-probe=$<TARGET_FILE:test-extractor-audio>)
    set_tests_properties(python-extractor PROPERTIES LABELS "integration;python;media" RUN_SERIAL TRUE)
    if(TARGET tests)
        add_dependencies(tests ascii-chat test-yt-dlp-fixture test-extractor-audio)
        if(TARGET test-client-ping-sentinel)
            add_dependencies(tests test-client-ping-sentinel)
        endif()
    endif()
endif()

if(BUILD_TESTS AND UNIX AND NOT APPLE AND TARGET ascii-chat)
    asciichat_add_pytest(python-media-sessions integration/media_sessions --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-media-sessions PROPERTIES LABELS "integration;python;media" RUN_SERIAL TRUE)
    set_property(TEST python-media-sessions APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "LSAN_OPTIONS=string_append::suppressions=${CMAKE_CURRENT_SOURCE_DIR}/standalone/network/x265_3_5_lsan.supp")
    if(TARGET tests)
        add_dependencies(tests ascii-chat)
    endif()
endif()

if(BUILD_TESTS AND UNIX AND TARGET ascii-chat)
    asciichat_add_pytest(python-synthetic-audio integration/synthetic_audio --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-synthetic-audio PROPERTIES LABELS "integration;python;audio" RUN_SERIAL TRUE)
    asciichat_add_pytest(python-kitty integration/kitty --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-kitty PROPERTIES LABELS "integration;python;media" RUN_SERIAL TRUE)
endif()

# Replace only the Argon2 boundary to observe real shared-handle interleavings.
if(BUILD_CRITERION_TESTS AND CRITERION_FOUND AND TARGET ascii-chat-static)
    add_executable(test-discovery-password-lock contracts/discovery/password_lock_test.c)
    target_link_libraries(test-discovery-password-lock PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES}
        ${LIBSODIUM_LIBRARIES} ${SQLITE3_LIBRARIES})
    target_include_directories(test-discovery-password-lock PRIVATE ${CMAKE_BINARY_DIR}/generated)
    apply_sanitizers_to_target(test-discovery-password-lock)
    set_target_properties(test-discovery-password-lock PROPERTIES LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    asciichat_configure_contract(test-discovery-password-lock)
    asciichat_add_contract_test(discovery-password-lock test-discovery-password-lock)
endif()

# Preserve each suite's existing category while making all migrated contracts selectable.
get_property(contract_tests DIRECTORY PROPERTY ASCIICHAT_CONTRACT_TESTS)
foreach(contract_test IN LISTS contract_tests)
    get_property(category TEST ${contract_test} PROPERTY LABELS)
    if(NOT category MATCHES "(^|;)(unit|integration|performance)(;|$)")
        set_property(TEST ${contract_test} APPEND PROPERTY LABELS integration)
    endif()
    set_property(TEST ${contract_test} APPEND PROPERTY LABELS criterion contract)
endforeach()

if(BUILD_TESTS AND TARGET ascii-chat)
    asciichat_add_pytest(python-cli integration/cli --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-cli PROPERTIES LABELS "integration;python;cli")
endif()

if(BUILD_TESTS AND TARGET ascii-chat AND TARGET ascii-chat-shared)
    asciichat_add_pytest(python-pattern integration/pattern --ascii-chat-binary=$<TARGET_FILE:ascii-chat>
        --shared-library=$<TARGET_FILE:ascii-chat-shared>)
    set_tests_properties(python-pattern PROPERTIES LABELS "integration;python")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND ASCIICHAT_SANITIZER_COMPILE_FLAGS MATCHES "fsanitize=address"
       AND EXISTS "${ASAN_RUNTIME_LIBRARY}")
        set_property(TEST python-pattern APPEND PROPERTY ENVIRONMENT
            "LD_PRELOAD=${ASAN_RUNTIME_LIBRARY}" "PYTHONMALLOC=malloc")
    endif()
endif()

if(BUILD_TESTS AND TARGET ascii-chat-shared)
    find_program(ASCIICHAT_TEST_FFMPEG ffmpeg)
    find_program(ASCIICHAT_TEST_FFPROBE ffprobe)
    set(shared_api_tests)
    asciichat_add_pytest(python-native native --shared-library=$<TARGET_FILE:ascii-chat-shared> --c-compiler=${CMAKE_C_COMPILER})
    set_tests_properties(python-native PROPERTIES LABELS "unit;python" ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1")
    list(APPEND shared_api_tests python-native)
    foreach(api_test IN LISTS shared_api_tests)
        set_tests_properties(${api_test} PROPERTIES TIMEOUT 900)
        set_property(TEST ${api_test} APPEND PROPERTY LABELS unit)
        # Python hosts the instrumented library, so load ASan before Python's allocator.
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND ASCIICHAT_SANITIZER_COMPILE_FLAGS MATCHES "fsanitize=address"
           AND EXISTS "${ASAN_RUNTIME_LIBRARY}")
            set_property(TEST ${api_test} APPEND PROPERTY ENVIRONMENT
                "LD_PRELOAD=${ASAN_RUNTIME_LIBRARY}" "PYTHONMALLOC=malloc")
        endif()
    endforeach()
endif()

if(BUILD_TESTS AND TARGET test-sync-regressions AND NOT WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    asciichat_add_pytest(python-sync integration/sync --sync-probe=$<TARGET_FILE:test-sync-regressions>)
    set_tests_properties(python-sync PROPERTIES LABELS "integration;python;terminal" RUN_SERIAL TRUE)
endif()

if(BUILD_TESTS AND TARGET ascii-chat)
    asciichat_add_pytest(python-first-use integration/first_use --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-first-use PROPERTIES LABELS "integration;python;terminal" RUN_SERIAL TRUE)
    set(final_error_args --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    if(CMAKE_BUILD_TYPE MATCHES "^(Debug|Dev|RelWithDebInfo)$")
        list(APPEND final_error_args --ui-diagnostics)
    endif()
    asciichat_add_pytest(python-final-error integration/final_error ${final_error_args})
    set_tests_properties(python-final-error PROPERTIES LABELS "integration;python;terminal" RUN_SERIAL TRUE)
endif()

if(BUILD_TESTS AND TARGET ascii-chat AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    asciichat_add_pytest(python-titles integration/titles --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-titles PROPERTIES LABELS "integration;python;terminal" RUN_SERIAL TRUE)
    if(ASCIICHAT_TEST_FFMPEG)
        asciichat_add_pytest(python-url-title integration/url_title --ascii-chat-binary=$<TARGET_FILE:ascii-chat>
            --ffmpeg=${ASCIICHAT_TEST_FFMPEG})
        set_tests_properties(python-url-title PROPERTIES LABELS "integration;python;terminal;media" RUN_SERIAL TRUE)
    endif()
endif()

if(BUILD_TESTS AND TARGET ascii-chat AND NOT WIN32 AND ASCIICHAT_TEST_FFMPEG AND ASCIICHAT_TEST_FFPROBE)
    set(terminal_ui_args --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND terminal_ui_args --debug-sync --debug-updates)
    endif()
    asciichat_add_pytest(python-terminal-ui integration/terminal_ui ${terminal_ui_args})
    set_tests_properties(python-terminal-ui PROPERTIES LABELS "integration;python;terminal" RUN_SERIAL TRUE)
endif()

if(BUILD_TESTS AND TARGET ascii-chat AND ASCIICHAT_TEST_FFMPEG AND ASCIICHAT_TEST_FFPROBE)
    asciichat_add_pytest(python-stereo integration/stereo --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-stereo PROPERTIES LABELS "integration;python;audio" RUN_SERIAL TRUE)
    asciichat_add_pytest(python-terminal-logs integration/terminal_logs --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-terminal-logs PROPERTIES LABELS "integration;python;terminal" RUN_SERIAL TRUE)
    if(TARGET ui-native-probe)
        asciichat_add_pytest(python-native-ui integration/native_ui --ui-native-probe=$<TARGET_FILE:ui-native-probe>)
        set_tests_properties(python-native-ui PROPERTIES LABELS "integration;python;terminal" TIMEOUT 600 RUN_SERIAL TRUE)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        asciichat_add_pytest(python-tui-modes integration/tui_modes --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
        set_tests_properties(python-tui-modes PROPERTIES LABELS "integration;python;terminal" RUN_SERIAL TRUE)
    endif()
    asciichat_add_pytest(python-lazy-audio integration/lazy_audio --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-lazy-audio PROPERTIES LABELS "integration;python;terminal;hardware" RUN_SERIAL TRUE)
endif()

if(TEST test_unit_crypto_test_gpg_key_expiry)
    set_tests_properties(test_unit_crypto_test_gpg_key_expiry PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

if(BUILD_TESTS AND TARGET gotel)
    add_executable(test-otel-contract standalone/media/otel_contract_test.c)
    # The Debug ascii-chat-static facade selects the shared library. This
    # white-box host must share one link image with the exercised production code.
    target_link_libraries(test-otel-contract PRIVATE ascii-chat-static-lib-with-deps gotel::testing ${MIMALLOC_LIBRARIES})
    target_include_directories(test-otel-contract PRIVATE ${CMAKE_BINARY_DIR}/generated)
    apply_sanitizers_to_target(test-otel-contract)
    set_target_properties(test-otel-contract PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BUILD_CRITERION_TESTS AND CRITERION_FOUND)
        add_executable(test-otel-adapter-contract contracts/otel/adapter_test.c)
        asciichat_configure_contract(test-otel-adapter-contract)
        target_link_libraries(test-otel-adapter-contract PRIVATE ascii-chat-static-lib-with-deps gotel::testing ${MIMALLOC_LIBRARIES})
        target_include_directories(test-otel-adapter-contract PRIVATE ${CMAKE_BINARY_DIR}/generated)
        apply_sanitizers_to_target(test-otel-adapter-contract)
        set_target_properties(test-otel-adapter-contract PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        asciichat_add_contract_test(otel-contract test-otel-adapter-contract)
        set_property(TEST otel-contract APPEND PROPERTY LABELS integration otel)
    else()
        add_test(NAME otel-contract COMMAND test-otel-contract)
        set_tests_properties(otel-contract PROPERTIES LABELS "integration;otel;contract" TIMEOUT 60)
    endif()
    asciichat_add_pytest(python-otel-contract integration/otel_contract --otel-contract-probe=$<TARGET_FILE:test-otel-contract>)
    set_tests_properties(python-otel-contract PROPERTIES LABELS "integration;python;otel;contract")
    if(TARGET tests)
        add_dependencies(tests test-otel-contract)
    endif()
    add_executable(test-otel-media standalone/media/otel_media_test.c)
    target_link_libraries(test-otel-media PRIVATE ascii-chat-static ${MIMALLOC_LIBRARIES} ${FFMPEG_LIBRARIES})
    target_include_directories(test-otel-media PRIVATE ${CMAKE_BINARY_DIR}/generated)
    apply_sanitizers_to_target(test-otel-media)
    set_target_properties(test-otel-media PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(TARGET tests)
        add_dependencies(tests test-otel-media)
    endif()
    asciichat_add_pytest(python-disabled-telemetry integration/disabled_telemetry)
    set_tests_properties(python-disabled-telemetry PROPERTIES LABELS "integration;python;otel")

endif()

if(BUILD_TESTS AND TARGET gotel)
    asciichat_add_pytest(python-telemetry integration/telemetry --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-telemetry PROPERTIES LABELS "integration;python;otel")
endif()

# Every registered test and native child runs with the canonical no-export policy.
function(asciichat_apply_test_environment directory)
    get_property(directory_tests DIRECTORY "${directory}" PROPERTY TESTS)
    foreach(test_name IN LISTS directory_tests)
        set_property(TEST ${test_name} DIRECTORY "${directory}" APPEND PROPERTY ENVIRONMENT "TESTING=1")
    endforeach()
    get_property(child_directories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS child_directories)
        asciichat_apply_test_environment("${child}")
    endforeach()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL asciichat_apply_test_environment "${CMAKE_SOURCE_DIR}")
get_property(asciichat_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(test_target IN LISTS asciichat_test_targets)
    get_target_property(test_type ${test_target} TYPE)
    if(test_type STREQUAL "EXECUTABLE")
        target_compile_definitions(${test_target} PRIVATE TESTING=1)
    endif()
endforeach()
foreach(collector_test otel-export otel-media otel-tls)
    if(TEST ${collector_test})
        set_tests_properties(${collector_test} PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endforeach()

if(BUILD_TESTS AND TARGET ascii-chat)
    asciichat_add_pytest(python-password integration/password --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-password PROPERTIES LABELS "integration;python;terminal" RUN_SERIAL TRUE)
endif()


if(BUILD_TESTS AND TARGET ascii-chat AND NOT EMSCRIPTEN)
    asciichat_add_pytest(python-mcp integration/mcp --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-mcp PROPERTIES LABELS "integration;python;mcp")
endif()

if(BUILD_TESTS AND TARGET ascii-chat AND CMAKE_BUILD_TYPE MATCHES "^(Debug|Dev|RelWithDebInfo)$")
    asciichat_add_pytest(python-mcp-tui integration/mcp_tui --ascii-chat-binary=$<TARGET_FILE:ascii-chat>)
    set_tests_properties(python-mcp-tui PROPERTIES LABELS "integration;python;mcp;tui" RUN_SERIAL TRUE)
endif()

if(BUILD_TESTS AND TARGET ascii-panic-report AND NOT EMSCRIPTEN)
    asciichat_add_pytest(python-panic integration/panic --panic-report-probe=$<TARGET_FILE:ascii-panic-report>)
    set_tests_properties(python-panic PROPERTIES LABELS "integration;python;tooling")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND ASCIICHAT_SANITIZER_COMPILE_FLAGS MATCHES "fsanitize=address"
       AND EXISTS "${ASAN_RUNTIME_LIBRARY}")
        set_property(TEST python-panic APPEND PROPERTY ENVIRONMENT
            "LD_PRELOAD=${ASAN_RUNTIME_LIBRARY}" "PYTHONMALLOC=malloc")
    endif()
    foreach(_test_build_target IN ITEMS tests tests-criterion-build tests-lib-build)
        if(TARGET ${_test_build_target})
            add_dependencies(${_test_build_target} ascii-panic-report)
        endif()
    endforeach()
endif()
