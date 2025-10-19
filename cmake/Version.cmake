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
# =============================================================================

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
    else()
        # Exactly on tag (0 commits) - release version
        set(GIT_VERSION_STRING \"\${GIT_DIRTY_SUFFIX}\")
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
    set(GIT_VERSION_STRING \"\${GIT_DIRTY_SUFFIX}\")
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
    set(PROJECT_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    set(PROJECT_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    set(PROJECT_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    set(PROJECT_VERSION \"${PROJECT_VERSION}\")
else()
    # Fallback - use version from project()
    set(GIT_VERSION_STRING \"unknown\")
    set(PROJECT_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    set(PROJECT_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    set(PROJECT_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    set(PROJECT_VERSION \"${PROJECT_VERSION}\")
endif()

# Set build type and OS - these get substituted at file write time
set(VERSION_BUILD_TYPE \"${CMAKE_BUILD_TYPE}\")
set(VERSION_OS \"${CMAKE_SYSTEM_NAME}\")

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
