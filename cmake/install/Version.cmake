# =============================================================================
# Version.cmake - Git-based version generation (nvim-style)
# =============================================================================
# Generates version information from git tags with -dev- suffix for development builds
#
# Version format:
#   - Release (on exact tag):     v0.2.0
#   - Development (after tag):    v0.2.0-dev-23+g63ae057-dirty
#   - No tags (commit only):      v0.1.0-dev-g63ae057-dirty
#
# Based on neovim's versioning scheme where -dev- indicates non-release builds.
#
# Usage:
#   include(cmake/install/Version.cmake)
#   version_detect()           # Call BEFORE project() - sets GIT_VERSION_* variables
#   project(... VERSION ${PROJECT_VERSION_FROM_GIT} ...)
#   version_setup_targets()    # Call AFTER project() - creates version.h generation
# =============================================================================

include_guard(GLOBAL)

# =============================================================================
# version_detect() - Call BEFORE project()
# Sets: PROJECT_VERSION_FROM_GIT, GIT_VERSION_MAJOR/MINOR/PATCH
# =============================================================================
macro(version_detect)
    # Get git describe output at configure time
    execute_process(
        COMMAND git describe --tags --long --dirty --always
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_DESCRIBE_CONFIGURE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # Parse git describe output
    # Format: v0.3.2-6-g0b715d6-dirty
    if(GIT_DESCRIBE_CONFIGURE MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)-([0-9]+)-g([0-9a-f]+)(-dirty)?$")
        # Has a tag with commits since - extract tag version
        set(GIT_VERSION_MAJOR "${CMAKE_MATCH_1}")
        set(GIT_VERSION_MINOR "${CMAKE_MATCH_2}")
        set(GIT_VERSION_PATCH "${CMAKE_MATCH_3}")
        set(PROJECT_VERSION_FROM_GIT "${GIT_VERSION_MAJOR}.${GIT_VERSION_MINOR}.${GIT_VERSION_PATCH}")
        set(_VERSION_TYPE "development")
    elseif(GIT_DESCRIBE_CONFIGURE MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)(-dirty)?$")
        # Exactly on a tag (no -N-g suffix) - release version
        set(GIT_VERSION_MAJOR "${CMAKE_MATCH_1}")
        set(GIT_VERSION_MINOR "${CMAKE_MATCH_2}")
        set(GIT_VERSION_PATCH "${CMAKE_MATCH_3}")
        set(PROJECT_VERSION_FROM_GIT "${GIT_VERSION_MAJOR}.${GIT_VERSION_MINOR}.${GIT_VERSION_PATCH}")
        set(_VERSION_TYPE "release")
    else()
        # No tags or git not available - use fallback
        set(GIT_VERSION_MAJOR "0")
        set(GIT_VERSION_MINOR "0")
        set(GIT_VERSION_PATCH "0")
        set(PROJECT_VERSION_FROM_GIT "0.0.0")
        set(_VERSION_TYPE "fallback")
    endif()

    # Print version info (colors defined in Init.cmake)
    if(_VERSION_TYPE STREQUAL "development")
        message(STATUS "Version from ${BoldBlue}git${ColorReset}: ${BoldGreen}${PROJECT_VERSION_FROM_GIT}${ColorReset} ${Yellow}(development)${ColorReset}")
    elseif(_VERSION_TYPE STREQUAL "release")
        message(STATUS "Version from ${BoldBlue}git${ColorReset}: ${BoldGreen}${PROJECT_VERSION_FROM_GIT}${ColorReset} ${BoldCyan}(release)${ColorReset}")
    else()
        message(STATUS "Version from ${BoldBlue}git${ColorReset}: ${BoldRed}not available${ColorReset} (using fallback: ${BoldYellow}${PROJECT_VERSION_FROM_GIT}${ColorReset})")
    endif()
endmacro()

# =============================================================================
# library_version_detect() - Detect library version from lib/v* tags
# Sets: ASCIICHAT_LIB_VERSION, ASCIICHAT_LIB_VERSION_MAJOR/MINOR/PATCH
# Call AFTER version_detect() but can be called before or after project()
# =============================================================================
macro(library_version_detect)
    # Get the highest lib/v* tag (sorted by version)
    execute_process(
        COMMAND git tag -l "lib/v[0-9]*.[0-9]*.[0-9]*" --sort=-v:refname
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE _LIB_TAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # Extract the first (highest) tag
    if(_LIB_TAGS)
        string(REGEX MATCH "^lib/v([0-9]+)\\.([0-9]+)\\.([0-9]+)" _LIB_TAG_MATCH "${_LIB_TAGS}")
        if(_LIB_TAG_MATCH)
            set(ASCIICHAT_LIB_VERSION_MAJOR "${CMAKE_MATCH_1}")
            set(ASCIICHAT_LIB_VERSION_MINOR "${CMAKE_MATCH_2}")
            set(ASCIICHAT_LIB_VERSION_PATCH "${CMAKE_MATCH_3}")
            set(ASCIICHAT_LIB_VERSION "${ASCIICHAT_LIB_VERSION_MAJOR}.${ASCIICHAT_LIB_VERSION_MINOR}.${ASCIICHAT_LIB_VERSION_PATCH}")
            set(_LIB_VERSION_TYPE "tagged")
        else()
            # Fallback if regex didn't match
            set(ASCIICHAT_LIB_VERSION_MAJOR "0")
            set(ASCIICHAT_LIB_VERSION_MINOR "1")
            set(ASCIICHAT_LIB_VERSION_PATCH "0")
            set(ASCIICHAT_LIB_VERSION "0.1.0")
            set(_LIB_VERSION_TYPE "fallback")
        endif()
    else()
        # No lib/v* tags found - use fallback
        set(ASCIICHAT_LIB_VERSION_MAJOR "0")
        set(ASCIICHAT_LIB_VERSION_MINOR "1")
        set(ASCIICHAT_LIB_VERSION_PATCH "0")
        set(ASCIICHAT_LIB_VERSION "0.1.0")
        set(_LIB_VERSION_TYPE "fallback")
    endif()

    # Print library version info
    if(_LIB_VERSION_TYPE STREQUAL "tagged")
        message(STATUS "Library version from ${BoldBlue}git${ColorReset} tag ${BoldMagenta}lib/v${ASCIICHAT_LIB_VERSION}${ColorReset}: ${BoldGreen}${ASCIICHAT_LIB_VERSION}${ColorReset}")
    else()
        message(STATUS "Library version: ${BoldYellow}${ASCIICHAT_LIB_VERSION}${ColorReset} (no lib/v* tags found)")
    endif()
endmacro()

# =============================================================================
# version_setup_targets() - Call AFTER project()
# Creates: generate_version target, version.h generation
# =============================================================================
function(version_setup_targets)
    # Create a script that generates version.h on every build
    set(VERSION_SCRIPT_PATH "${CMAKE_BINARY_DIR}/generate_version.cmake")
    file(WRITE "${VERSION_SCRIPT_PATH}" "
# Get git describe output (includes commits since last tag)
execute_process(
    COMMAND git describe --tags --long --dirty --always
    WORKING_DIRECTORY \"${CMAKE_SOURCE_DIR}\"
    OUTPUT_VARIABLE GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# Parse git describe output
# Format: v0.2.0-42-gf811525-dirty
# where 0.2.0 is the tag, 42 is commits since tag, g prefix indicates git hash
if(GIT_DESCRIBE MATCHES \"^v?([0-9]+)\\\\.([0-9]+)\\\\.([0-9]+)-([0-9]+)-g([0-9a-f]+)(-dirty)?\\\$\")
    # Has a tag with commits since - extract tag version
    set(TAG_VERSION_MAJOR \"\${CMAKE_MATCH_1}\")
    set(TAG_VERSION_MINOR \"\${CMAKE_MATCH_2}\")
    set(TAG_VERSION_PATCH \"\${CMAKE_MATCH_3}\")
    set(GIT_COMMITS_SINCE_TAG \"\${CMAKE_MATCH_4}\")
    set(GIT_COMMIT_HASH \"\${CMAKE_MATCH_5}\")
    set(GIT_DIRTY_SUFFIX \"\${CMAKE_MATCH_6}\")
    # Add -dev- prefix if commits > 0 (development version, like neovim)
    if(NOT \"\${GIT_COMMITS_SINCE_TAG}\" STREQUAL \"0\")
        set(GIT_VERSION_STRING \"dev-\${GIT_COMMITS_SINCE_TAG}+g\${GIT_COMMIT_HASH}\${GIT_DIRTY_SUFFIX}\")
        set(GIT_VERSION_SUFFIX \"-\")  # Separator for non-release versions
    else()
        # Exactly on tag (0 commits) - release version
        if(\"\${GIT_DIRTY_SUFFIX}\" STREQUAL \"-dirty\")
            set(GIT_VERSION_STRING \"dirty\")
            set(GIT_VERSION_SUFFIX \"-\")  # Separator for dirty releases
        else()
            set(GIT_VERSION_STRING \"\")
            set(GIT_VERSION_SUFFIX \"\")  # No separator for clean releases
        endif()
    endif()
    # Override PROJECT_VERSION with tag version
    set(PROJECT_VERSION_MAJOR \"\${TAG_VERSION_MAJOR}\")
    set(PROJECT_VERSION_MINOR \"\${TAG_VERSION_MINOR}\")
    set(PROJECT_VERSION_PATCH \"\${TAG_VERSION_PATCH}\")
    set(PROJECT_VERSION \"\${TAG_VERSION_MAJOR}.\${TAG_VERSION_MINOR}.\${TAG_VERSION_PATCH}\")
elseif(GIT_DESCRIBE MATCHES \"^v?([0-9]+)\\\\.([0-9]+)\\\\.([0-9]+)(-dirty)?\\\$\")
    # Exactly on a tag (no -N-g suffix) - release version
    set(TAG_VERSION_MAJOR \"\${CMAKE_MATCH_1}\")
    set(TAG_VERSION_MINOR \"\${CMAKE_MATCH_2}\")
    set(TAG_VERSION_PATCH \"\${CMAKE_MATCH_3}\")
    set(GIT_DIRTY_SUFFIX \"\${CMAKE_MATCH_4}\")
    if(\"\${GIT_DIRTY_SUFFIX}\" STREQUAL \"-dirty\")
        set(GIT_VERSION_STRING \"dirty\")
        set(GIT_VERSION_SUFFIX \"-\")  # Separator for dirty releases
    else()
        set(GIT_VERSION_STRING \"\")
        set(GIT_VERSION_SUFFIX \"\")  # No separator for clean releases
    endif()
    # Override PROJECT_VERSION with tag version
    set(PROJECT_VERSION_MAJOR \"\${TAG_VERSION_MAJOR}\")
    set(PROJECT_VERSION_MINOR \"\${TAG_VERSION_MINOR}\")
    set(PROJECT_VERSION_PATCH \"\${TAG_VERSION_PATCH}\")
    set(PROJECT_VERSION \"\${TAG_VERSION_MAJOR}.\${TAG_VERSION_MINOR}.\${TAG_VERSION_PATCH}\")
elseif(GIT_DESCRIBE MATCHES \"^([0-9a-f]+)(-dirty)?\\\$\")
    # No tags, just commit hash - development version with fallback version from project()
    set(GIT_COMMIT_HASH \"\${CMAKE_MATCH_1}\")
    set(GIT_DIRTY_SUFFIX \"\${CMAKE_MATCH_2}\")
    set(GIT_VERSION_STRING \"dev-g\${GIT_COMMIT_HASH}\${GIT_DIRTY_SUFFIX}\")
    set(GIT_VERSION_SUFFIX \"-\")  # Separator for development versions
    set(PROJECT_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    set(PROJECT_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    set(PROJECT_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    set(PROJECT_VERSION \"${PROJECT_VERSION}\")
else()
    # Fallback - use version from project()
    set(GIT_VERSION_STRING \"unknown\")
    set(GIT_VERSION_SUFFIX \"-\")  # Separator for unknown versions
    set(PROJECT_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    set(PROJECT_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    set(PROJECT_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    set(PROJECT_VERSION \"${PROJECT_VERSION}\")
endif()

# Set build type and OS - these get substituted at file write time
set(VERSION_BUILD_TYPE \"${CMAKE_BUILD_TYPE}\")
set(VERSION_OS \"${CMAKE_SYSTEM_NAME}\")

# Get git commit hash and dirty state
execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY \"${CMAKE_SOURCE_DIR}\"
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH \"unknown\")
endif()

# Check if working tree is dirty
execute_process(
    COMMAND git diff-index --quiet HEAD --
    WORKING_DIRECTORY \"${CMAKE_SOURCE_DIR}\"
    RESULT_VARIABLE GIT_DIRTY_RESULT
    ERROR_QUIET
)
if(GIT_DIRTY_RESULT EQUAL 0)
    set(GIT_IS_DIRTY \"false\")
else()
    set(GIT_IS_DIRTY \"true\")
endif()

# Get current date in yyyy-mm-dd format
string(TIMESTAMP BUILD_DATE \"%Y-%m-%d\" UTC)

# For Release builds, compute hash of all tracked repository files
if(\"${CMAKE_BUILD_TYPE}\" STREQUAL \"Release\")
    # Get list of all tracked files from git
    execute_process(
        COMMAND git ls-files
        WORKING_DIRECTORY \"${CMAKE_SOURCE_DIR}\"
        OUTPUT_VARIABLE GIT_TRACKED_FILES
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(GIT_TRACKED_FILES)
        # Convert newline-separated list to CMake list
        string(REPLACE \"\\n\" \";\" FILE_LIST \"\${GIT_TRACKED_FILES}\")

        # Compute SHA256 hash of each file and concatenate
        set(ALL_HASHES \"\")
        foreach(FILE_PATH \${FILE_LIST})
            set(FULL_PATH \"${CMAKE_SOURCE_DIR}/\${FILE_PATH}\")
            if(EXISTS \"\${FULL_PATH}\" AND NOT IS_DIRECTORY \"\${FULL_PATH}\")
                file(SHA256 \"\${FULL_PATH}\" FILE_HASH)
                string(APPEND ALL_HASHES \"\${FILE_HASH}\")
            endif()
        endforeach()

        # Compute final hash of all concatenated hashes
        string(SHA256 REPO_HASH \"\${ALL_HASHES}\")
    else()
        set(REPO_HASH \"unknown\")
    endif()
else()
    # Non-Release builds: no repository hash (empty string)
    set(REPO_HASH \"\")
endif()

# Generate version header to temp file first
# This will be copied to final location only if content changed
configure_file(
    \"${CMAKE_SOURCE_DIR}/lib/version.h.in\"
    \"${CMAKE_BINARY_DIR}/generated/version.h.tmp\"
    @ONLY
)
")

    # Add custom command that regenerates version.h smartly based on build type
    # - Debug/Dev: Only regenerate when .git/HEAD changes (commits/branch changes)
    # - Release: Regenerate when any tracked files change (for reproducible builds)
    #
    # Strategy: Always generate to temp file, then copy_if_different to preserve timestamps
    #
    # Handle git worktrees: .git may be a file containing "gitdir: <path>" instead of a directory
    # In worktrees, HEAD and index are in the gitdir, not .git/
    if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/.git")
        # Normal git repository
        set(_GIT_DIR "${CMAKE_SOURCE_DIR}/.git")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/.git")
        # Git worktree: .git is a file containing "gitdir: <path>"
        file(READ "${CMAKE_SOURCE_DIR}/.git" _GITDIR_CONTENT)
        string(REGEX REPLACE "gitdir: ([^\n]+)\n?" "\\1" _GIT_DIR "${_GITDIR_CONTENT}")
        string(STRIP "${_GIT_DIR}" _GIT_DIR)
        # Handle relative paths
        if(NOT IS_ABSOLUTE "${_GIT_DIR}")
            set(_GIT_DIR "${CMAKE_SOURCE_DIR}/${_GIT_DIR}")
        endif()
    else()
        # No git directory found - use fallback that won't cause build errors
        set(_GIT_DIR "${CMAKE_SOURCE_DIR}/.git")
    endif()

    if(CMAKE_BUILD_TYPE MATCHES "^(Debug|Dev)$")
        # Debug/Dev: Only depend on .git/HEAD (commits/branches), NOT .git/index
        # This prevents rebuilds from uncommitted changes or staging
        set(VERSION_DEPENDENCIES
            "${_GIT_DIR}/HEAD"
            "${CMAKE_SOURCE_DIR}/lib/version.h.in"
        )
    else()
        # Release: Also depend on .git/index for dirty state tracking
        set(VERSION_DEPENDENCIES
            "${_GIT_DIR}/HEAD"
            "${_GIT_DIR}/index"
            "${CMAKE_SOURCE_DIR}/lib/version.h.in"
        )
    endif()

    add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/generated/version.h"
        # Generate to temp file first
        COMMAND ${CMAKE_COMMAND} -P "${VERSION_SCRIPT_PATH}"
        # Copy temp to final location only if different (preserves timestamp if unchanged)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/generated/version.h.tmp"
            "${CMAKE_BINARY_DIR}/generated/version.h"
        # Clean up temp file
        COMMAND ${CMAKE_COMMAND} -E remove -f "${CMAKE_BINARY_DIR}/generated/version.h.tmp"
        DEPENDS ${VERSION_DEPENDENCIES}
        COMMENT "Generating version header with current git state..."
        VERBATIM
    )

    # Add custom target that depends on the generated header
    # Note: NOT using ALL here - only regenerates when dependencies change
    add_custom_target(generate_version
        DEPENDS "${CMAKE_BINARY_DIR}/generated/version.h"
    )
endfunction()
