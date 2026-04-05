# =============================================================================
# yyjson - Fast JSON library
# =============================================================================
# yyjson is a fast JSON library used for structured logging output (writer)
# and GitHub API response parsing in the update checker (reader).
#
# Build Strategy:
#   1. Try to find system package via CMake config and pkg-config
#   2. If not found, build from submodule source (deps/ascii-chat-deps/yyjson)
#   3. Build as static library

# Helper: set yyjson build options, add_subdirectory, and validate the target.
# Must be called from within configure_yyjson() since it sets PARENT_SCOPE vars
# (which propagate to configure_yyjson's caller).
macro(_yyjson_build_from_submodule source_dir label)
    if(NOT EXISTS "${${source_dir}}/CMakeLists.txt")
        message(FATAL_ERROR "yyjson submodule not found at ${${source_dir}}\n"
                            "Did you run 'git submodule update --init --recursive'?")
    endif()

    # Build options — disable unused features to reduce binary size
    set(YYJSON_DISABLE_READER OFF CACHE BOOL "Enable JSON reader for update checker" FORCE)
    set(YYJSON_DISABLE_UTILS ON CACHE BOOL "Disable JSON Pointer/Patch/Merge utilities" FORCE)
    set(YYJSON_DISABLE_INCR_READER ON CACHE BOOL "Disable incremental reader" FORCE)
    set(YYJSON_BUILD_TESTS OFF CACHE BOOL "Disable yyjson tests" FORCE)
    set(YYJSON_BUILD_FUZZER OFF CACHE BOOL "Disable yyjson fuzzer" FORCE)
    set(YYJSON_BUILD_MISC OFF CACHE BOOL "Disable yyjson misc" FORCE)
    set(YYJSON_BUILD_DOC OFF CACHE BOOL "Disable yyjson documentation" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build yyjson as static library" FORCE)

    add_subdirectory(
        "${${source_dir}}"
        "${CMAKE_CURRENT_BINARY_DIR}/yyjson-build"
    )

    if(NOT TARGET yyjson)
        message(FATAL_ERROR "yyjson target not created by subdirectory")
    endif()

    set(YYJSON_LIBRARIES yyjson PARENT_SCOPE)
    set(YYJSON_INCLUDE_DIRS "${${source_dir}}/src" PARENT_SCOPE)
    set(YYJSON_FOUND TRUE PARENT_SCOPE)

    message(STATUS "Configured ${BoldGreen}yyjson${ColorReset} from submodule (${label})")
    message(STATUS "  Disabled: UTILS, INCR_READER, TESTS, FUZZER, MISC, DOC")
endmacro()

function(configure_yyjson)
    # iOS builds always use submodule source
    if(PLATFORM_IOS)
        message(STATUS "Configuring ${BoldBlue}yyjson${ColorReset} from submodule (iOS)...")
        set(YYJSON_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/yyjson")
        _yyjson_build_from_submodule(YYJSON_SOURCE_DIR "iOS")
        return()
    endif()

    # Musl builds always use submodule source
    if(USE_MUSL)
        message(STATUS "Configuring ${BoldBlue}yyjson${ColorReset} from submodule (musl)...")
        set(YYJSON_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/yyjson")
        _yyjson_build_from_submodule(YYJSON_SOURCE_DIR "musl")
        return()
    endif()

    # Non-musl: Try pkg-config first (finds shared lib with reader enabled),
    # then fall back to CMake config. The CMake config at /usr/local may point
    # to a static lib built with the reader disabled, causing undefined symbols.
    include(FindPkgConfig)
    pkg_check_modules(yyjson QUIET yyjson)

    if(yyjson_FOUND AND NOT TARGET yyjson::yyjson)
        add_library(yyjson::yyjson INTERFACE IMPORTED)
        target_include_directories(yyjson::yyjson INTERFACE ${yyjson_INCLUDE_DIRS})
        target_link_libraries(yyjson::yyjson INTERFACE ${yyjson_LIBRARIES})
    endif()

    if(NOT yyjson_FOUND)
        # Fall back to CMake config if pkg-config not found
        find_package(yyjson QUIET CONFIG)
    endif()

    # If still not found, build from submodule source
    if(NOT yyjson_FOUND)
        message(STATUS "System yyjson not found, building from source...")
        set(YYJSON_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/yyjson")
        _yyjson_build_from_submodule(YYJSON_SOURCE_DIR "source")
        return()
    endif()

    # System package found
    set(YYJSON_LIBRARIES yyjson::yyjson PARENT_SCOPE)

    # Use pkg-config include dirs if available, otherwise find the header
    if(NOT yyjson_INCLUDE_DIRS)
        find_path(YYJSON_INCLUDE yyjson.h PATHS /usr/include /usr/local/include /home/linuxbrew/.linuxbrew/include)
        set(yyjson_INCLUDE_DIRS ${YYJSON_INCLUDE})
    endif()

    set(YYJSON_INCLUDE_DIRS ${yyjson_INCLUDE_DIRS} PARENT_SCOPE)

    message(STATUS "Configured ${BoldGreen}yyjson${ColorReset} from system package")
    message(STATUS "  YYJSON_LIBRARIES: yyjson::yyjson")
    message(STATUS "  YYJSON_INCLUDE_DIRS: ${yyjson_INCLUDE_DIRS}")
endfunction()
