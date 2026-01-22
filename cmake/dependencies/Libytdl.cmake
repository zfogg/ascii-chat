##############################################################################
# libytdl Dependency Configuration
#
# libytdl (YouTube DL core) is a REQUIRED dependency for YouTube URL support.
# It provides the ytdlcore library for extracting direct stream URLs from
# YouTube videos.
#
# Dependencies:
#   - LibXml2 (for HTML parsing)
#   - QuickJS (bundled in libytdl)
#   - yyjson (bundled in libytdl)
#
# This file:
#   1. Checks for LibXml2 dependency
#   2. Configures libytdl build (ytdlcore only, no HTTP client or muxer)
#   3. Creates the ytdlcore library target
#   4. Sets up include paths and libraries
#
##############################################################################

set(LIBYTDL_FOUND FALSE CACHE BOOL "" FORCE)

# ============================================================================
# Check for Required Dependencies
# ============================================================================

# LibXml2 is required by libytdl's ytdlcore
find_package(LibXml2 QUIET)

if(NOT LIBXML2_FOUND)
  message(FATAL_ERROR
    "libytdl (YouTube URL extraction) requires LibXml2 to be installed.\n"
    "Please install LibXml2 development files:\n"
    "  macOS:   brew install libxml2\n"
    "  Ubuntu:  sudo apt install libxml2-dev\n"
    "  Fedora:  sudo dnf install libxml2-devel\n"
    "  Windows: vcpkg install libxml2:x64-windows\n"
  )
endif()

message(STATUS "Found LibXml2: ${LIBXML2_INCLUDE_DIR}")

# ============================================================================
# Verify libytdl Submodule is Initialized
# ============================================================================

set(YTDL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libytdl")

if(NOT EXISTS "${YTDL_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR
    "libytdl submodule not found at: ${YTDL_SOURCE_DIR}\n"
    "Please initialize git submodules:\n"
    "  git submodule update --init --recursive\n"
  )
endif()

message(STATUS "Found libytdl at: ${YTDL_SOURCE_DIR}")

# ============================================================================
# Configure libytdl Build
# ============================================================================

# Build static library (easier to link than shared on multiple platforms)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build libytdl as static library" FORCE)

# Build only ytdlcore (video info extraction), disable HTTP client and muxer
# because we use FFmpeg for HTTP and muxing
set(YTDL_BUILD_HTTP OFF CACHE BOOL "Disable libytdl HTTP client (use FFmpeg instead)" FORCE)
set(YTDL_BUILD_MUX OFF CACHE BOOL "Disable libytdl muxer (use FFmpeg instead)" FORCE)
set(YTDL_BUILD_CLI OFF CACHE BOOL "Disable libytdl CLI" FORCE)
set(YTDL_BUILD_REGEXC OFF CACHE BOOL "Disable libytdl regex compile tool" FORCE)

message(STATUS "Configuring libytdl: ytdlcore=ON http=OFF muxer=OFF cli=OFF")

# ============================================================================
# Add libytdl as Subdirectory
# ============================================================================

# Add libytdl to the build
add_subdirectory("${YTDL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/libytdl" EXCLUDE_FROM_ALL)

# ============================================================================
# Create Interface Target for Easy Linking
# ============================================================================

# The actual libraries built by libytdl's CMakeLists.txt
# Typically: ytdlcore, regexp (bundled regex library)
# These names come from libytdl's CMakeLists.txt output

set(LIBYTDL_LIBRARIES ytdlcore regexp)
set(LIBYTDL_INCLUDE_DIRS
  "${YTDL_SOURCE_DIR}/include"
  "${YTDL_SOURCE_DIR}/src"
  "${LIBXML2_INCLUDE_DIR}"
)

message(STATUS "libytdl libraries: ${LIBYTDL_LIBRARIES}")
message(STATUS "libytdl include paths: ${LIBYTDL_INCLUDE_DIRS}")

# ============================================================================
# Verify Targets Exist
# ============================================================================

# Check that ytdlcore target was created by libytdl's CMakeLists.txt
if(NOT TARGET ytdlcore)
  message(FATAL_ERROR
    "libytdl CMakeLists.txt did not create ytdlcore target.\n"
    "Check libytdl submodule is properly initialized and CMakeLists.txt exists.\n"
  )
endif()

message(STATUS "✓ libytdl ytdlcore target created")

# ============================================================================
# Export Configuration
# ============================================================================

set(LIBYTDL_FOUND TRUE CACHE BOOL "libytdl found and configured" FORCE)

# Define HAVE_LIBYTDL compile flag for conditional code
add_compile_definitions(HAVE_LIBYTDL)

# Add include directories globally so libytdl headers are available
include_directories(${LIBYTDL_INCLUDE_DIRS})

message(STATUS "✓ libytdl configured successfully (HAVE_LIBYTDL defined)")
