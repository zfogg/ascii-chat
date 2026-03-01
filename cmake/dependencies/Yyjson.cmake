# =============================================================================
# yyjson - Fast JSON library (writer-only)
# =============================================================================
# yyjson is a fast JSON library used for structured logging output.
# We use only the writer API to generate JSON logs; reading is disabled
# to reduce binary size.
#
# Build Strategy:
#   1. Try to find system package via CMake config and pkg-config
#   2. If not found, build from submodule source (deps/ascii-chat-deps/yyjson)
#   3. Disable reader functionality to reduce binary size
#   4. Build as static library

function(configure_yyjson)
    # Musl builds always use submodule source
    if(USE_MUSL)
        message(STATUS "Configuring ${BoldBlue}yyjson${ColorReset} from submodule (musl)...")

        set(YYJSON_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/yyjson")

        if(NOT EXISTS "${YYJSON_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "yyjson submodule not found at ${YYJSON_SOURCE_DIR}\n"
                                "Did you run 'git submodule update --init --recursive'?")
        endif()

        # Disable extra features to reduce binary size
        set(YYJSON_DISABLE_READER ON CACHE BOOL "Disable JSON reader (only using writer)" FORCE)
        set(YYJSON_DISABLE_UTILS ON CACHE BOOL "Disable JSON Pointer/Patch/Merge utilities" FORCE)
        set(YYJSON_DISABLE_INCR_READER ON CACHE BOOL "Disable incremental reader" FORCE)
        set(YYJSON_BUILD_TESTS OFF CACHE BOOL "Disable yyjson tests" FORCE)
        set(YYJSON_BUILD_FUZZER OFF CACHE BOOL "Disable yyjson fuzzer" FORCE)
        set(YYJSON_BUILD_MISC OFF CACHE BOOL "Disable yyjson misc" FORCE)
        set(YYJSON_BUILD_DOC OFF CACHE BOOL "Disable yyjson documentation" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build yyjson as static library" FORCE)

        add_subdirectory(
            "${YYJSON_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/yyjson-build"
            EXCLUDE_FROM_ALL
        )

        if(NOT TARGET yyjson)
            message(FATAL_ERROR "yyjson target not created by subdirectory")
        endif()

        if(NOT TARGET yyjson::yyjson)
            add_library(yyjson::yyjson ALIAS yyjson)
        endif()

        set(YYJSON_LIBRARIES yyjson::yyjson PARENT_SCOPE)
        set(YYJSON_INCLUDE_DIRS "${YYJSON_SOURCE_DIR}/src" PARENT_SCOPE)
        set(YYJSON_FOUND TRUE PARENT_SCOPE)

        message(STATUS "Configured ${BoldGreen}yyjson${ColorReset} from submodule (musl)")
        message(STATUS "  Disabled: READER, UTILS, INCR_READER, TESTS, FUZZER, MISC, DOC")
        return()
    endif()

    # Non-musl: Try system package first, then submodule fallback
    # Try to find yyjson via CMake config first, then fall back to pkg-config
    find_package(yyjson QUIET CONFIG)

    if(NOT yyjson_FOUND)
        # Fall back to pkg-config if CMake config not found
        include(FindPkgConfig)
        pkg_check_modules(yyjson QUIET yyjson)

        # Create interface library for compatibility
        if(yyjson_FOUND AND NOT TARGET yyjson::yyjson)
            add_library(yyjson::yyjson INTERFACE IMPORTED)
            target_include_directories(yyjson::yyjson INTERFACE ${yyjson_INCLUDE_DIRS})
            target_link_libraries(yyjson::yyjson INTERFACE ${yyjson_LIBRARIES})
        endif()
    endif()

    # If still not found, build from submodule source
    if(NOT yyjson_FOUND)
        message(STATUS "System yyjson not found, building from source (deps/ascii-chat-deps/yyjson)")

        set(YYJSON_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/yyjson")

        if(NOT EXISTS "${YYJSON_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "yyjson submodule not found at ${YYJSON_SOURCE_DIR}\n"
                                "Did you run 'git submodule update --init --recursive'?")
        endif()

        # Disable extra features to reduce binary size
        # We only use the JSON writer API for structured logging
        set(YYJSON_DISABLE_READER ON CACHE BOOL "Disable JSON reader (only using writer)" FORCE)
        set(YYJSON_DISABLE_UTILS ON CACHE BOOL "Disable JSON Pointer/Patch/Merge utilities" FORCE)
        set(YYJSON_DISABLE_INCR_READER ON CACHE BOOL "Disable incremental reader" FORCE)
        set(YYJSON_BUILD_TESTS OFF CACHE BOOL "Disable yyjson tests" FORCE)
        set(YYJSON_BUILD_FUZZER OFF CACHE BOOL "Disable yyjson fuzzer" FORCE)
        set(YYJSON_BUILD_MISC OFF CACHE BOOL "Disable yyjson misc" FORCE)
        set(YYJSON_BUILD_DOC OFF CACHE BOOL "Disable yyjson documentation" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build yyjson as static library" FORCE)

        # Add yyjson as subdirectory with disabled features
        add_subdirectory(
            "${YYJSON_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/yyjson-build"
            EXCLUDE_FROM_ALL
        )

        # yyjson target is created by the subproject's CMakeLists.txt
        if(NOT TARGET yyjson)
            message(FATAL_ERROR "yyjson target not created by subdirectory")
        endif()

        if(NOT TARGET yyjson::yyjson)
            add_library(yyjson::yyjson ALIAS yyjson)
        endif()

        set(YYJSON_LIBRARIES yyjson::yyjson PARENT_SCOPE)
        set(YYJSON_INCLUDE_DIRS "${YYJSON_SOURCE_DIR}/src" PARENT_SCOPE)
        set(YYJSON_FOUND TRUE PARENT_SCOPE)
        message(STATUS "Configured ${BoldGreen}yyjson${ColorReset} from source (with disabled features)")
        message(STATUS "  Source: ${YYJSON_SOURCE_DIR}")
        message(STATUS "  Build directory: ${CMAKE_CURRENT_BINARY_DIR}/yyjson-build")
        message(STATUS "  Disabled: READER, UTILS, INCR_READER, TESTS, FUZZER, MISC, DOC")
        message(STATUS "  Build type: static library")

        return()
    endif()

    # yyjson::yyjson target is imported by find_package(yyjson) or created above
    # We export it to parent scope for use in target_link_libraries
    set(YYJSON_LIBRARIES yyjson::yyjson PARENT_SCOPE)

    # Find include directory
    find_path(YYJSON_INCLUDE yyjson.h)
    set(YYJSON_INCLUDE_DIRS ${YYJSON_INCLUDE} PARENT_SCOPE)

    message(STATUS "Configured ${BoldGreen}yyjson${ColorReset} from system package")
    message(STATUS "  YYJSON_LIBRARIES: yyjson::yyjson")
    message(STATUS "  YYJSON_INCLUDE_DIRS: ${YYJSON_INCLUDE}")
endfunction()
