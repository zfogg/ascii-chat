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

# Find clang-format executable
find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)

if(CLANG_FORMAT_EXECUTABLE)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${ALL_SOURCE_FILES}
        COMMENT "Formatting source code with clang-format"
        VERBATIM
    )

    # Alias target: clang-format (same as format)
    add_custom_target(clang-format
        COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${ALL_SOURCE_FILES}
        COMMENT "Formatting source code with clang-format"
        VERBATIM
    )

    # Format check target
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run -Werror ${ALL_SOURCE_FILES}
        COMMENT "Checking code formatting"
        VERBATIM
    )
else()
    message(WARNING "${BoldRed}clang-format not found.${ColorReset} Format targets will not be available.")
    add_custom_target(format
        COMMAND ${CMAKE_COMMAND} -E echo "${BoldRed}clang-format not found.${ColorReset} Please install clang-format."
    )
    add_custom_target(clang-format
        COMMAND ${CMAKE_COMMAND} -E echo "${BoldRed}clang-format not found.${ColorReset} Please install clang-format."
    )
    add_custom_target(format-check
        COMMAND ${CMAKE_COMMAND} -E echo "${BoldRed}clang-format not found.${ColorReset} Please install clang-format."
    )
endif()

# Find Python3 and run-clang-tidy.py script
find_program(PYTHON3_EXECUTABLE NAMES python3 python)
find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)

# Check if run-clang-tidy.py script exists
set(RUN_CLANG_TIDY_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/run-clang-tidy.py")

if(PYTHON3_EXECUTABLE AND EXISTS "${RUN_CLANG_TIDY_SCRIPT}" AND CLANG_TIDY_EXECUTABLE)
    # Use run-clang-tidy.py for parallel execution and better project file filtering
    # The script automatically filters to lib/, src/, tests/ directories (project code only)
    # It uses compile_commands.json from the build directory
    # Use CPU_CORES if available (set in BuildConfiguration.cmake), otherwise default to 4
    if(DEFINED CPU_CORES)
        set(CLANG_TIDY_JOBS ${CPU_CORES})
    else()
        set(CLANG_TIDY_JOBS 4)
        message(STATUS "CPU_CORES not defined, using ${BoldYellow}${CLANG_TIDY_JOBS}${ColorReset} parallel jobs for clang-tidy")
    endif()

    add_custom_target(clang-tidy
        COMMAND ${CLANG_TIDY_EXECUTABLE}
            --verify-config
            --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy
        COMMAND ${PYTHON3_EXECUTABLE}
            ${RUN_CLANG_TIDY_SCRIPT}
            -p ${CMAKE_BINARY_DIR}
            -config-file ${CMAKE_SOURCE_DIR}/.clang-tidy
            -j ${CLANG_TIDY_JOBS}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Verifying clang-tidy config, then running static analysis on production code (excluding tests) using run-clang-tidy.py (parallel: ${CLANG_TIDY_JOBS} jobs)"
        VERBATIM
    )
elseif(CLANG_TIDY_EXECUTABLE)
    # Fallback to direct clang-tidy if script not available
    message(STATUS "run-clang-tidy.py not found, using direct clang-tidy (slower, no parallel execution)")
    add_custom_target(clang-tidy
        COMMAND ${CLANG_TIDY_EXECUTABLE}
            --verify-config
            --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy
        COMMAND ${CLANG_TIDY_EXECUTABLE}
            --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy
            -p ${CMAKE_BINARY_DIR}
            ${PRODUCTION_SOURCE_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Verifying clang-tidy config, then running static analysis on production code (excluding tests)"
        VERBATIM
    )
else()
    message(WARNING "clang-tidy not found. Clang-tidy target will not be available.")
    add_custom_target(clang-tidy
        COMMAND ${CMAKE_COMMAND} -E echo "clang-tidy not found. Please install clang-tidy."
    )
endif()

# Find scan-build executable
find_program(SCAN_BUILD_EXECUTABLE NAMES scan-build)

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
# Shared Library Build Target (Release builds)
# =============================================================================
# Build shared library (libasciichat.so/.dylib/asciichat.dll)
# Usage: cmake --build build_release --target shared-lib
#
# Note: Only available for Release builds (Debug/Dev builds already include shared library)
# The ascii-chat-shared target is EXCLUDE_FROM_ALL in Release mode, so this provides
# an easy way to build it explicitly.
# =============================================================================
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(WIN32)
        set(SHARED_LIB_OUTPUT "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/asciichat.dll")
    else()
        set(SHARED_LIB_OUTPUT "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libasciichat${CMAKE_SHARED_LIBRARY_SUFFIX}")
    endif()

    # Wrapper target that builds shared library
    add_custom_target(shared-lib
        COMMAND ${CMAKE_COMMAND} -DACTION=check -DTARGET_NAME=ascii-chat-shared -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/BuildTimer.cmake
        DEPENDS ascii-chat-shared build-timer-start
        VERBATIM
    )
endif()

