# =============================================================================
# Test Library Executables
# =============================================================================
# Simple test programs to verify both static and shared libraries work correctly
# Both targets use the same source file but link against different libraries

# Helper: detect mimalloc include directory for test harnesses
set(_asciichat_mimalloc_include "")
if(DEFINED MIMALLOC_INCLUDE_DIR AND MIMALLOC_INCLUDE_DIR)
    list(APPEND _asciichat_mimalloc_include ${MIMALLOC_INCLUDE_DIR})
elseif(DEFINED MIMALLOC_SOURCE_DIR AND MIMALLOC_SOURCE_DIR)
    list(APPEND _asciichat_mimalloc_include "${MIMALLOC_SOURCE_DIR}/include")
endif()

# =============================================================================
# Test Static Library
# =============================================================================
# Only available when building STATIC libraries (not OBJECT libraries)
# For Debug/Dev/Coverage: EXCLUDE_FROM_ALL (build only on explicit request)
# For Release: Build by default (since Release uses static library)
if(NOT BUILDING_OBJECT_LIBS)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_executable(test-static-lib ${CMAKE_SOURCE_DIR}/cmake/test/test_lib.c)
        set(TEST_STATIC_DEFAULT "built by default")
    else()
        add_executable(test-static-lib EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/cmake/test/test_lib.c)
        set(TEST_STATIC_DEFAULT "explicit target only")
    endif()

    # Link against the static library (mimalloc is transitively linked)
    target_link_libraries(test-static-lib PRIVATE
        ascii-chat-static-lib
        ascii-chat-defer
    )

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
    add_executable(test-shared-lib ${CMAKE_SOURCE_DIR}/cmake/test/test_lib.c)

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

    message(STATUS "Added test-shared-lib target to test asciichat.dll/.so")

    # Add a custom test that runs test-shared-lib to verify it works
    add_custom_target(run-test-shared-lib
        COMMAND ${CMAKE_BINARY_DIR}/bin/test-shared-lib
        DEPENDS test-shared-lib
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running test-shared-lib to verify shared library works"
        VERBATIM
    )

    add_test(NAME shared-lib-runtime
             COMMAND ${CMAKE_BINARY_DIR}/bin/test-shared-lib)
else()
    message(STATUS "Skipping test-shared-lib (not compatible with musl static builds)")
endif()
