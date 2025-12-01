# =============================================================================
# Custom Targets Module
# =============================================================================
# This module creates custom targets for code formatting, static analysis, and cleaning
#
# Prerequisites:
#   - Source files exist
#
# Outputs:
#   - format, clang-format, format-check targets
#   - clang-tidy target
#   - scan-build, scan-build-view targets
#   - clean_all target
# =============================================================================

# Format target - cross-platform version
file(GLOB_RECURSE ALL_SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/src/**/*.c
    ${CMAKE_SOURCE_DIR}/src/**/*.h
    ${CMAKE_SOURCE_DIR}/src/*.c
    ${CMAKE_SOURCE_DIR}/src/*.h
    ${CMAKE_SOURCE_DIR}/lib/**/*.c
    ${CMAKE_SOURCE_DIR}/lib/**/*.h
    ${CMAKE_SOURCE_DIR}/lib/*.c
    ${CMAKE_SOURCE_DIR}/lib/*.h
    ${CMAKE_SOURCE_DIR}/tests/**/*.c
    ${CMAKE_SOURCE_DIR}/tests/**/*.h
    ${CMAKE_SOURCE_DIR}/tests/*.c
    ${CMAKE_SOURCE_DIR}/tests/*.h
)

# Include Objective-C files on Apple platforms
if(APPLE)
    file(GLOB_RECURSE OBJC_SOURCE_FILES
        ${CMAKE_SOURCE_DIR}/lib/**/*.m
        ${CMAKE_SOURCE_DIR}/lib/*.m
        ${CMAKE_SOURCE_DIR}/src/**/*.m
        ${CMAKE_SOURCE_DIR}/src/*.m
    )
    list(APPEND ALL_SOURCE_FILES ${OBJC_SOURCE_FILES})
endif()

# Create production source files list (exclude tests for clang-tidy)
file(GLOB_RECURSE PRODUCTION_SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/src/**/*.c
    ${CMAKE_SOURCE_DIR}/src/**/*.h
    ${CMAKE_SOURCE_DIR}/src/*.c
    ${CMAKE_SOURCE_DIR}/src/*.h
    ${CMAKE_SOURCE_DIR}/lib/**/*.c
    ${CMAKE_SOURCE_DIR}/lib/**/*.h
    ${CMAKE_SOURCE_DIR}/lib/*.c
    ${CMAKE_SOURCE_DIR}/lib/*.h
)

# Include Objective-C files in production source files on Apple platforms
if(APPLE)
    file(GLOB_RECURSE OBJC_PRODUCTION_FILES
        ${CMAKE_SOURCE_DIR}/lib/**/*.m
        ${CMAKE_SOURCE_DIR}/lib/*.m
        ${CMAKE_SOURCE_DIR}/src/**/*.m
        ${CMAKE_SOURCE_DIR}/src/*.m
    )
    list(APPEND PRODUCTION_SOURCE_FILES ${OBJC_PRODUCTION_FILES})
endif()

# Filter out third-party or generated files if needed
# Note: Objective-C files (.m) are included for Apple platforms

# Use centralized clang-format from FindPrograms.cmake
set(CLANG_FORMAT_EXECUTABLE "${ASCIICHAT_CLANG_FORMAT_EXECUTABLE}")

if(CLANG_FORMAT_EXECUTABLE)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${ALL_SOURCE_FILES}
        COMMENT "Formatting source code with clang-format"
        VERBATIM
    )

    if(NOT TARGET clang-format)
        add_custom_target(clang-format
            COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${ALL_SOURCE_FILES}
            COMMENT "Formatting source code with clang-format"
            VERBATIM
        )
    endif()

    if(NOT TARGET format-check)
        add_custom_target(format-check
            COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run -Werror ${ALL_SOURCE_FILES}
            COMMENT "Checking code formatting"
            VERBATIM
        )
    endif()
else()
    message(WARNING "${BoldRed}clang-format not found.${ColorReset} Format targets will not be available.")
    add_custom_target(format
        COMMAND ${CMAKE_COMMAND} -E echo "${BoldRed}clang-format not found.${ColorReset} Please install clang-format."
    )
    if(NOT TARGET clang-format)
        add_custom_target(clang-format
            COMMAND ${CMAKE_COMMAND} -E echo "${BoldRed}clang-format not found.${ColorReset} Please install clang-format."
        )
    endif()
    if(NOT TARGET format-check)
        add_custom_target(format-check
            COMMAND ${CMAKE_COMMAND} -E echo "${BoldRed}clang-format not found.${ColorReset} Please install clang-format."
        )
    endif()
endif()

if(NOT TARGET clang-format)
    add_custom_target(clang-format
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/cmake/utils/RunClangFormat.cmake
        COMMENT "Running clang-format on source files"
        VERBATIM
    )
endif()

if(NOT TARGET clang-format-check)
    add_custom_target(clang-format-check
        COMMAND ${CMAKE_COMMAND} -DONLY_CHECK=ON -P ${CMAKE_SOURCE_DIR}/cmake/utils/RunClangFormat.cmake
        COMMENT "Checking clang-format formatting"
        VERBATIM
    )
endif()

# Use centralized clang-tidy from FindPrograms.cmake
set(CLANG_TIDY_EXECUTABLE "${ASCIICHAT_CLANG_TIDY_EXECUTABLE}")
set(RUN_CLANG_TIDY_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/run-clang-tidy.py")

set(_clang_tidy_missing_deps "")
if(NOT ASCIICHAT_PYTHON3_EXECUTABLE)
    list(APPEND _clang_tidy_missing_deps "python3 interpreter")
endif()
if(NOT CLANG_TIDY_EXECUTABLE)
    list(APPEND _clang_tidy_missing_deps "clang-tidy executable")
endif()
if(NOT EXISTS "${RUN_CLANG_TIDY_SCRIPT}")
    list(APPEND _clang_tidy_missing_deps "scripts/run-clang-tidy.py helper")
endif()

if(NOT _clang_tidy_missing_deps)
    if(DEFINED CPU_CORES)
        set(CLANG_TIDY_JOBS ${CPU_CORES})
    else()
        set(CLANG_TIDY_JOBS 4)
        message(STATUS "CPU_CORES not defined, using ${BoldYellow}${CLANG_TIDY_JOBS}${ColorReset} parallel jobs for clang-tidy")
    endif()

    if(NOT TARGET clang-tidy)
        # clang-tidy must run after build to ensure PCH files and generated sources exist
        add_custom_target(clang-tidy
            COMMAND ${CLANG_TIDY_EXECUTABLE}
                --verify-config
                --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy
            COMMAND ${ASCIICHAT_PYTHON3_EXECUTABLE}
                ${RUN_CLANG_TIDY_SCRIPT}
                -clang-tidy-binary ${CLANG_TIDY_EXECUTABLE}
                -p ${CMAKE_BINARY_DIR}
                -config-file ${CMAKE_SOURCE_DIR}/.clang-tidy
                -j ${CLANG_TIDY_JOBS}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            DEPENDS ascii-chat
            COMMENT "Verifying clang-tidy config, then running static analysis on production code (excluding tests) via scripts/run-clang-tidy.py (parallel: ${CLANG_TIDY_JOBS} jobs)"
            VERBATIM
        )
    endif()
else()
    list(JOIN _clang_tidy_missing_deps ", " _clang_tidy_missing_deps_msg)
    message(WARNING "clang-tidy target disabled: missing ${_clang_tidy_missing_deps_msg}.")
    if(NOT TARGET clang-tidy)
        add_custom_target(clang-tidy
            COMMAND ${CMAKE_COMMAND} -E echo "clang-tidy target unavailable: missing ${_clang_tidy_missing_deps_msg}."
        )
    endif()
endif()

# Use centralized scan-build from FindPrograms.cmake
set(SCAN_BUILD_EXECUTABLE "${ASCIICHAT_SCAN_BUILD_EXECUTABLE}")

if(SCAN_BUILD_EXECUTABLE)
    add_custom_target(scan-build
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/scan-build-results
        COMMAND ${SCAN_BUILD_EXECUTABLE}
            --use-analyzer=${CMAKE_C_COMPILER}
            -o ${CMAKE_BINARY_DIR}/scan-build-results
            ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ascii-chat
        COMMENT "Running scan-build static analyzer"
        VERBATIM
    )

    add_custom_target(scan-build-view
        COMMAND ${CMAKE_COMMAND} -E echo "Opening scan-build results in browser..."
        COMMAND open ${CMAKE_BINARY_DIR}/scan-build-results/*/index.html || xdg-open ${CMAKE_BINARY_DIR}/scan-build-results/*/index.html || start ${CMAKE_BINARY_DIR}/scan-build-results/*/index.html
        DEPENDS scan-build
        COMMENT "Opening scan-build results"
        VERBATIM
    )
else()
    message(WARNING "scan-build not found. Scan-build targets will not be available.")
    add_custom_target(scan-build
        COMMAND ${CMAKE_COMMAND} -E echo "scan-build not found. Please install clang static analyzer."
    )
endif()

# Clean all target (matches Makefile clean)
add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/bin
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/lib
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/CMakeFiles
    COMMENT "Cleaning all build artifacts"
)

# =============================================================================
# Shared Library Build Target (All build types)
# =============================================================================
# Build shared library (libasciichat.so/.dylib/asciichat.dll)
# Usage: cmake --build build --target shared-lib
#
# Note:
# - Debug/Dev/Coverage: shared library is built by default (part of ascii-chat-static)
# - Release: shared library is EXCLUDE_FROM_ALL, use this target to build it explicitly
# =============================================================================
if(WIN32)
    set(SHARED_LIB_OUTPUT "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/asciichat.dll")
else()
    set(SHARED_LIB_OUTPUT "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libasciichat${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()

# Wrapper target that builds shared library
add_custom_target(shared-lib
    COMMAND ${CMAKE_COMMAND} -DACTION=check -DTARGET_NAME=ascii-chat-shared -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    DEPENDS ascii-chat-shared build-timer-start
    VERBATIM
)

