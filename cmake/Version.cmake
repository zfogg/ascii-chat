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
# This module:
#   1. Detects version from git at configure time (overrides PROJECT_VERSION)
#   2. Generates version.h at build time (for runtime version display)
# =============================================================================

# =============================================================================
# Configure-time version detection (for CMake variables like PROJECT_VERSION)
# =============================================================================
# Get git describe output at configure time
execute_process(
    COMMAND git describe --tags --long --dirty --always
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_DESCRIBE_CONFIGURE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# Parse git describe output and override PROJECT_VERSION
# Format: v0.3.2-6-g0b715d6-dirty
if(GIT_DESCRIBE_CONFIGURE MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)-([0-9]+)-g([0-9a-f]+)(-dirty)?$")
    # Has a tag with commits since - extract tag version
    set(GIT_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(GIT_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(GIT_VERSION_PATCH "${CMAKE_MATCH_3}")
    message(STATUS "Version from git: ${GIT_VERSION_MAJOR}.${GIT_VERSION_MINOR}.${GIT_VERSION_PATCH} (development)")
    # Override PROJECT_VERSION with git-detected version
    set(PROJECT_VERSION_MAJOR "${GIT_VERSION_MAJOR}" CACHE STRING "Major version from git" FORCE)
    set(PROJECT_VERSION_MINOR "${GIT_VERSION_MINOR}" CACHE STRING "Minor version from git" FORCE)
    set(PROJECT_VERSION_PATCH "${GIT_VERSION_PATCH}" CACHE STRING "Patch version from git" FORCE)
    set(PROJECT_VERSION "${GIT_VERSION_MAJOR}.${GIT_VERSION_MINOR}.${GIT_VERSION_PATCH}" CACHE STRING "Version from git" FORCE)
elseif(GIT_DESCRIBE_CONFIGURE MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)(-dirty)?$")
    # Exactly on a tag (no -N-g suffix) - release version
    set(GIT_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(GIT_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(GIT_VERSION_PATCH "${CMAKE_MATCH_3}")
    message(STATUS "Version from git: ${GIT_VERSION_MAJOR}.${GIT_VERSION_MINOR}.${GIT_VERSION_PATCH} (release)")
    # Override PROJECT_VERSION with git-detected version
    set(PROJECT_VERSION_MAJOR "${GIT_VERSION_MAJOR}" CACHE STRING "Major version from git" FORCE)
    set(PROJECT_VERSION_MINOR "${GIT_VERSION_MINOR}" CACHE STRING "Minor version from git" FORCE)
    set(PROJECT_VERSION_PATCH "${GIT_VERSION_PATCH}" CACHE STRING "Patch version from git" FORCE)
    set(PROJECT_VERSION "${GIT_VERSION_MAJOR}.${GIT_VERSION_MINOR}.${GIT_VERSION_PATCH}" CACHE STRING "Version from git" FORCE)
else()
    # No tags or git not available - use fallback from project() or keep existing
    message(STATUS "Version from git: not available (using fallback: ${PROJECT_VERSION})")
endif()

# Create a script that generates version.h on every build
set(VERSION_SCRIPT "${CMAKE_BINARY_DIR}/generate_version.cmake")
file(WRITE "${VERSION_SCRIPT}" "
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

# Generate version header
configure_file(
    \"${CMAKE_SOURCE_DIR}/lib/version.h.in\"
    \"${CMAKE_BINARY_DIR}/generated/version.h\"
    @ONLY
)
")

# Add custom target that runs the version script on every build
add_custom_target(generate_version
    COMMAND ${CMAKE_COMMAND} -P "${VERSION_SCRIPT}"
    BYPRODUCTS "${CMAKE_BINARY_DIR}/generated/version.h"
    COMMENT "Generating version header with current git state..."
    VERBATIM
)
