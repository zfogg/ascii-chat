cmake_minimum_required(VERSION 3.24)

# This directory is intentionally usable in two modes:
#
#   Standalone (fast docs-only build):
#     cmake -S docs -B build/docs -DASCII_CHAT_SOURCE_DIR=...
#     cmake --build build/docs --target docs
#
#   Main project build: Documentation.cmake adds this directory and supplies the
#   repository/build roots. No native targets or third-party libraries are needed.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ascii-chat-docs NONE)
    if(NOT DEFINED ASCII_CHAT_SOURCE_DIR)
        get_filename_component(ASCII_CHAT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    endif()
    set(ASCII_CHAT_BINARY_DIR "${CMAKE_BINARY_DIR}")
    set(ASCIICHAT_DOCS_STANDALONE ON)
else()
    set(ASCIICHAT_DOCS_STANDALONE OFF)
endif()

get_filename_component(ASCII_CHAT_SOURCE_DIR "${ASCII_CHAT_SOURCE_DIR}" ABSOLUTE)
get_filename_component(ASCII_CHAT_BINARY_DIR "${ASCII_CHAT_BINARY_DIR}" ABSOLUTE)

find_package(Doxygen REQUIRED)

if(NOT DEFINED ASCII_CHAT_VERSION OR ASCII_CHAT_VERSION STREQUAL "")
    execute_process(
        COMMAND git -C "${ASCII_CHAT_SOURCE_DIR}" describe --tags --always --dirty
        OUTPUT_VARIABLE ASCII_CHAT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(ASCII_CHAT_VERSION STREQUAL "")
        set(ASCII_CHAT_VERSION "development")
    endif()
endif()

if(NOT DEFINED ASCII_CHAT_DOC_SHA OR ASCII_CHAT_DOC_SHA STREQUAL "")
    execute_process(
        COMMAND git -C "${ASCII_CHAT_SOURCE_DIR}" rev-parse --short=12 HEAD
        OUTPUT_VARIABLE ASCII_CHAT_DOC_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(ASCII_CHAT_DOC_SHA STREQUAL "")
        set(ASCII_CHAT_DOC_SHA "development")
    endif()
endif()

set(PROJECT_VERSION "${ASCII_CHAT_VERSION}")
set(PROJECT_NAME_FULL "ascii-chat")
set(PROJECT_BUNDLE_ID "com.zfo.gg.ascii-chat")
set(AWESOME_CSS_DIR "${ASCII_CHAT_SOURCE_DIR}/deps/doxygen-awesome-css")
set(DOXYLAYOUTFILE_OUT "${CMAKE_CURRENT_BINARY_DIR}/DoxygenLayout.xml")
set(DOXYFILE_HTML "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.html")
set(GENERATE_HTML "YES")
set(GENERATE_MAN "NO")
set(CUSTOM_THEME_FILENAME "${CMAKE_CURRENT_BINARY_DIR}/custom-theme-${ASCII_CHAT_DOC_SHA}.css")

file(MAKE_DIRECTORY
    "${ASCII_CHAT_BINARY_DIR}/share/doc/html"
    "${ASCII_CHAT_BINARY_DIR}/docs"
)

configure_file(
    "${ASCII_CHAT_SOURCE_DIR}/docs/custom-theme-v2.css"
    "${CUSTOM_THEME_FILENAME}"
    COPYONLY
)

configure_file(
    "${ASCII_CHAT_SOURCE_DIR}/docs/DoxygenLayout.xml.in"
    "${DOXYLAYOUTFILE_OUT}"
    @ONLY
)
configure_file(
    "${ASCII_CHAT_SOURCE_DIR}/docs/Doxyfile.in"
    "${DOXYFILE_HTML}"
    @ONLY
)

add_custom_target(docs-html
    COMMAND "${DOXYGEN_EXECUTABLE}" "${DOXYFILE_HTML}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Generating HTML documentation with Doxygen"
    VERBATIM
)

if(ASCIICHAT_DOCS_STANDALONE)
    add_custom_target(docs
        DEPENDS docs-html
        COMMENT "Documentation build complete"
    )
endif()

if(NOT ASCIICHAT_DOCS_STANDALONE)
    # Keep the main-build output location and the existing docs-open workflow.
    add_custom_target(docs-open
        COMMAND ${CMAKE_COMMAND} -E echo "Documentation generated in ${ASCII_CHAT_BINARY_DIR}/share/doc/html/index.html"
        DEPENDS docs-html
        VERBATIM
    )
endif()

message(STATUS "Documentation target 'docs' is available (docs-only CMake project)")
