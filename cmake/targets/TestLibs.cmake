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
        set(TEST_STATIC_DEFAULT "built by default")
    else()
        add_executable(test-static-lib EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/cmake/test/test_lib.c)
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
        add_dependencies(test-static-lib portaudio-musl alsa-lib-musl libsodium-musl zstd-musl libexecinfo-musl opus-musl speexdsp-musl)
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
# Available in all build types except musl static builds
# Musl builds use static-pie which cannot link against shared libraries
if(NOT USE_MUSL)
    if(BUILD_TESTS)
        add_executable(test-shared-lib ${CMAKE_SOURCE_DIR}/cmake/test/test_lib.c)
        set(TEST_SHARED_DEFAULT "built by default")
    else()
        add_executable(test-shared-lib EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/cmake/test/test_lib.c)
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
    endif()
else()
    message(STATUS "Skipping test-shared-lib (not compatible with musl static builds)")
endif()

