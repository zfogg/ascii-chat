# =============================================================================
# yyjson - Fast JSON library (writer-only)
# =============================================================================
# yyjson is a fast JSON library used for structured logging output.
# We use only the writer API to generate JSON logs; reading is disabled
# to reduce binary size.

function(configure_yyjson)
    find_package(yyjson REQUIRED)

    # yyjson::yyjson target is imported by find_package(yyjson)
    # We export it to parent scope for use in target_link_libraries
    set(YYJSON_LIBRARIES yyjson::yyjson PARENT_SCOPE)

    # Find include directory
    find_path(YYJSON_INCLUDE yyjson.h REQUIRED)
    set(YYJSON_INCLUDE_DIRS ${YYJSON_INCLUDE} PARENT_SCOPE)

    message(STATUS "Configured ${BoldGreen}yyjson${ColorReset} from system package")
    message(STATUS "  YYJSON_LIBRARIES: yyjson::yyjson")
    message(STATUS "  YYJSON_INCLUDE_DIRS: ${YYJSON_INCLUDE}")
endfunction()
