# =============================================================================
# Liburcu (userspace RCU) Configuration
# =============================================================================
# Finds and configures liburcu for lock-free concurrent data structures
#
# Liburcu is used for:
#   - Lock-free read-copy-update (RCU) synchronization
#   - RCU hash tables (cds_lfht) replacing uthash + rwlock
#   - High-performance session registry in ACDS
#
# Build strategy:
#   1. Check for system-installed liburcu (e.g., from package manager)
#   2. Build from source if system install not found
#   3. Cache built library for reuse across clean builds
#
# Prerequisites (must be set before including this file):
#   - WIN32, UNIX, APPLE: Platform detection variables
#   - CMAKE_BUILD_TYPE: Build type
#   - ASCIICHAT_DEPS_CACHE_DIR: Dependency cache directory
#
# Outputs (variables set by this file):
#   - LIBURCU_LIBRARIES: Libraries to link against
#   - LIBURCU_INCLUDE_DIRS: Include directories
#   - LIBURCU_FOUND: Whether liburcu was found or built successfully
# =============================================================================

include(ProcessorCount)
ProcessorCount(NPROC)
if(NPROC EQUAL 0)
    set(NPROC 1)
endif()

# Set cache directory for pre-built liburcu
set(LIBURCU_CACHE_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/liburcu")

# ============================================================================
# Try cached library first
# ============================================================================
if(EXISTS "${LIBURCU_CACHE_DIR}/lib/liburcu.a" AND
   EXISTS "${LIBURCU_CACHE_DIR}/include/urcu.h")
    message(STATUS "Using cached liburcu from ${LIBURCU_CACHE_DIR}")
    add_library(liburcu STATIC IMPORTED GLOBAL)
    set_target_properties(liburcu PROPERTIES
        IMPORTED_LOCATION "${LIBURCU_CACHE_DIR}/lib/liburcu.a"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBURCU_CACHE_DIR}/include"
    )
    set(LIBURCU_FOUND TRUE)
    return()
endif()

# ============================================================================
# Platform-specific discovery and build
# ============================================================================

if(WIN32)
    # Windows: Try vcpkg
    find_package(liburcu CONFIG QUIET)
    if(liburcu_FOUND)
        message(STATUS "Found liburcu via vcpkg")
        set(LIBURCU_FOUND TRUE)
        return()
    endif()
elseif(UNIX)
    # Unix (Linux/macOS): Try pkg-config first
    # Need BOTH liburcu-mb (for RCU) and liburcu-cds (for lock-free hash tables)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBURCU_MB QUIET liburcu-mb)
        pkg_check_modules(LIBURCU_CDS QUIET liburcu-cds)
        if(LIBURCU_MB_FOUND AND LIBURCU_CDS_FOUND)
            message(STATUS "Found liburcu via pkg-config: MB=${LIBURCU_MB_LIBRARIES} CDS=${LIBURCU_CDS_LIBRARIES}")
            # Create imported target for consistency
            add_library(liburcu INTERFACE IMPORTED GLOBAL)
            target_include_directories(liburcu INTERFACE ${LIBURCU_MB_INCLUDE_DIRS} ${LIBURCU_CDS_INCLUDE_DIRS})
            target_link_libraries(liburcu INTERFACE ${LIBURCU_MB_LIBRARIES} ${LIBURCU_CDS_LIBRARIES})
            return()
        endif()
    endif()

    # Unix: Try system search
    find_path(LIBURCU_INCLUDE_DIR NAMES urcu.h
              PATHS /usr/include /usr/local/include
              NO_DEFAULT_PATH)
    find_library(LIBURCU_LIBRARY NAMES urcu urcu-mb
                 PATHS /usr/lib /usr/local/lib
                 NO_DEFAULT_PATH)

    if(LIBURCU_INCLUDE_DIR AND LIBURCU_LIBRARY)
        message(STATUS "Found liburcu system install")
        add_library(liburcu STATIC IMPORTED GLOBAL)
        set_target_properties(liburcu PROPERTIES
            IMPORTED_LOCATION "${LIBURCU_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBURCU_INCLUDE_DIR}"
        )
        set(LIBURCU_FOUND TRUE)
        return()
    endif()
endif()

# ============================================================================
# Build from source if not found (Unix only)
# ============================================================================

if(NOT UNIX)
    message(FATAL_ERROR "liburcu required but not found (Windows support via vcpkg not yet implemented)")
endif()

message(STATUS "Building liburcu from source...")

# Check if git and build tools are available
find_program(GIT_EXECUTABLE git)
find_program(AUTOCONF_EXECUTABLE autoconf)
find_program(AUTOMAKE_EXECUTABLE automake)

if(NOT GIT_EXECUTABLE)
    message(FATAL_ERROR "git required to build liburcu from source")
endif()

if(NOT AUTOCONF_EXECUTABLE OR NOT AUTOMAKE_EXECUTABLE)
    message(FATAL_ERROR "autoconf and automake required to build liburcu")
endif()

include(ExternalProject)

set(LIBURCU_SOURCE_DIR "${LIBURCU_CACHE_DIR}/src")
set(LIBURCU_BUILD_DIR "${LIBURCU_CACHE_DIR}/build")

# Build configuration based on system architecture
set(URCU_FLAVOR "urcu-mb")  # memory barrier (simplest, good enough for most use cases)

# For aarch64, optionally use urcu-qsbr (quiescent state based RCU) but urcu-mb is fine
# For x86_64, urcu-mb is the recommended flavor

ExternalProject_Add(liburcu_external
    GIT_REPOSITORY https://github.com/urcu/userspace-rcu.git
    GIT_TAG v0.14.0
    PREFIX "${LIBURCU_CACHE_DIR}"
    SOURCE_DIR "${LIBURCU_SOURCE_DIR}"
    BINARY_DIR "${LIBURCU_BUILD_DIR}"
    CONFIGURE_COMMAND
        ${LIBURCU_SOURCE_DIR}/configure
            --prefix=${LIBURCU_CACHE_DIR}
            --enable-shared=no
            --enable-static=yes
            --disable-debug-rcu
    BUILD_COMMAND make -j${NPROC}
    INSTALL_COMMAND make install
)

# Create imported target that depends on the external project
add_library(liburcu STATIC IMPORTED GLOBAL)
add_dependencies(liburcu liburcu_external)

set_target_properties(liburcu PROPERTIES
    IMPORTED_LOCATION "${LIBURCU_CACHE_DIR}/lib/liburcu.a"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBURCU_CACHE_DIR}/include"
)

set(LIBURCU_FOUND TRUE)

message(STATUS "liburcu will be built from source to ${LIBURCU_CACHE_DIR}")
