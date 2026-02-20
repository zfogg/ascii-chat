# =============================================================================
# yyjson - Fast JSON library (writer-only)
# =============================================================================
# yyjson is a fast JSON library used for structured logging output.
# We use only the writer API to generate JSON logs; reading is disabled
# to reduce binary size.

function(configure_yyjson)
    # Try to find yyjson via CMake config first, then fall back to pkg-config
    find_package(yyjson QUIET CONFIG)

    if(NOT yyjson_FOUND)
        # Fall back to pkg-config if CMake config not found
        include(FindPkgConfig)
        pkg_check_modules(yyjson REQUIRED yyjson)

        # Create interface library for compatibility
        if(NOT TARGET yyjson::yyjson)
            add_library(yyjson::yyjson INTERFACE IMPORTED)
            target_include_directories(yyjson::yyjson INTERFACE ${yyjson_INCLUDE_DIRS})
            target_link_libraries(yyjson::yyjson INTERFACE ${yyjson_LIBRARIES})
        endif()
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
