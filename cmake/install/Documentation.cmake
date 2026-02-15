# =============================================================================
# Documentation Module
# =============================================================================
# This module creates targets for generating documentation:
#   - man1 target: Generates user manual pages (no Doxygen required)
#   - docs target: Generates full Doxygen API documentation (requires Doxygen)
#
# Prerequisites:
#   - man1: Requires ascii-chat binary to be built (uses --man-page-create option)
#   - docs: Doxygen executable must be installed
#
# Outputs:
#   - man1: ${CMAKE_BINARY_DIR}/share/man/man1/ascii-chat.1 (uncompressed for development)
#   - docs: ${CMAKE_BINARY_DIR}/docs/html/ and ${CMAKE_BINARY_DIR}/docs/man/man3/
# =============================================================================

# =============================================================================
# Man1 Target (User Manual - No Doxygen Required)
# =============================================================================
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/share/man/man1")

# Determine the executable path (handles Windows .exe extension)
if(WIN32)
    set(ASCII_CHAT_EXECUTABLE "${CMAKE_BINARY_DIR}/bin/ascii-chat.exe")
else()
    set(ASCII_CHAT_EXECUTABLE "${CMAKE_BINARY_DIR}/bin/ascii-chat")
endif()

# Generate ascii-chat man page at build time using --man-page-create option
# This merges the template (.1.in) with manual content (.1.content) and auto-generates
# option documentation from the options builder
# Note: --man-page-create writes to stdout with no argument or file with argument.
# Uncompressed .1 generated; packaging compresses at install time
#
# First, process the template to substitute version variables
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/share/man/man1")

# Extract year from PROJECT_VERSION_DATE (format: YYYY-MM-DD)
string(SUBSTRING "${PROJECT_VERSION_DATE}" 0 4 COPYRIGHT_YEAR_END)

# Configure man1 template first
configure_file(
    "${CMAKE_SOURCE_DIR}/share/man/man1/ascii-chat.1.in"
    "${CMAKE_BINARY_DIR}/share/man/man1/ascii-chat.1.in"
    @ONLY
)

add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/share/man/man1/ascii-chat.1"
    COMMAND bash -c "ASCII_CHAT_QUESTION_PROMPT_RESPONSE='y' ASCIICHAT_RESOURCE_DIR='${CMAKE_BINARY_DIR}' LSAN_OPTIONS=verbosity=0:halt_on_error=0 ASAN_OPTIONS=verbosity=0:halt_on_error=0 timeout -k 1 1.5 '${ASCII_CHAT_EXECUTABLE}' --man-page-create '${CMAKE_BINARY_DIR}/share/man/man1/ascii-chat.1'"
    DEPENDS
        $<TARGET_FILE:ascii-chat>
        "${CMAKE_BINARY_DIR}/share/man/man1/ascii-chat.1.in"
    COMMENT "Building man page"
    VERBATIM
)

# Build man pages target (works for both Debug and Release)
add_custom_target(man1 ALL
    DEPENDS "${CMAKE_BINARY_DIR}/share/man/man1/ascii-chat.1"
    COMMENT "Man pages build complete"
)

message(STATUS "Man1 target ${BoldCyan}'man1'${ColorReset} is available (no Doxygen required). Build with: ${BoldYellow}cmake --build build --target man1${ColorReset}")

# Use centralized ASCIICHAT_DOXYGEN_EXECUTABLE from FindPrograms.cmake
if(ASCIICHAT_DOXYGEN_EXECUTABLE)
    message(STATUS "Found ${BoldGreen}Doxygen${ColorReset}: ${ASCIICHAT_DOXYGEN_EXECUTABLE}")

    set(AWESOME_CSS_DIR "${CMAKE_SOURCE_DIR}/deps/doxygen-awesome-css")

    # Configure Doxyfile from template
    set(DOXYFILE_IN "${CMAKE_SOURCE_DIR}/docs/Doxyfile.in")
    set(DOXYFILE_OUT "${CMAKE_BINARY_DIR}/Doxyfile")
    set(DOXYLAYOUTFILE_IN "${CMAKE_SOURCE_DIR}/docs/DoxygenLayout.xml.in")
    set(DOXYLAYOUTFILE_OUT "${CMAKE_BINARY_DIR}/DoxygenLayout.xml")

    configure_file(
        ${DOXYFILE_IN}
        ${DOXYFILE_OUT}
        @ONLY
    )
    configure_file(
        ${DOXYLAYOUTFILE_IN}
        ${DOXYLAYOUTFILE_OUT}
        @ONLY
    )

    # Create docs directory if it doesn't exist
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/docs")

    # Generate manpage renaming script at configure time
    file(WRITE "${CMAKE_BINARY_DIR}/RenameManpages.cmake" "
# Rename manpages with ascii-chat- prefix
file(TO_CMAKE_PATH \"\${MAN_DIR}\" MAN_DIR)

# Find all .3 manpages in the directory
file(GLOB MANPAGES \"\${MAN_DIR}/*.3\")
if(NOT MANPAGES)
    message(STATUS \"No manpages found in \${MAN_DIR}, skipping\")
    return()
endif()

set(RENAMED_COUNT 0)
set(SKIPPED_COUNT 0)

foreach(MANPAGE \${MANPAGES})
    get_filename_component(FILENAME \"\${MANPAGE}\" NAME)
    if(FILENAME MATCHES \"^ascii-chat-\")
        math(EXPR SKIPPED_COUNT \"\${SKIPPED_COUNT} + 1\")
        continue()
    endif()

    set(NEW_FILENAME \"ascii-chat-\${FILENAME}\")
    get_filename_component(DIR \"\${MANPAGE}\" DIRECTORY)
    set(NEW_PATH \"\${DIR}/\${NEW_FILENAME}\")
    file(RENAME \"\${MANPAGE}\" \"\${NEW_PATH}\")
    math(EXPR RENAMED_COUNT \"\${RENAMED_COUNT} + 1\")
endforeach()

message(STATUS \"Manpage renaming: \${RENAMED_COUNT} renamed, \${SKIPPED_COUNT} already prefixed\")
")

    # Create documentation target (Doxygen output suppressed via QUIET = YES in Doxyfile.in)
    add_custom_target(docs
        COMMAND timeout 30 ${ASCIICHAT_DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
        COMMAND ${CMAKE_COMMAND} -E echo "Adding ascii-chat- prefix to manpages..."
        COMMAND ${CMAKE_COMMAND} -DMAN_DIR=${CMAKE_BINARY_DIR}/docs/man/man3 -P ${CMAKE_BINARY_DIR}/RenameManpages.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )

    # Create a target to open documentation in browser (cross-platform)
    if(WIN32)
        # Windows: use PowerShell Start-Process
        add_custom_target(docs-open
            COMMAND ${CMAKE_COMMAND} -E echo "Opening documentation in browser..."
            COMMAND powershell -Command "Start-Process \"${CMAKE_BINARY_DIR}/docs/html/index.html\""
            DEPENDS docs
            COMMENT "Opening documentation in default browser"
            VERBATIM
        )
    elseif(APPLE)
        # macOS: use open command
        add_custom_target(docs-open
            COMMAND ${CMAKE_COMMAND} -E echo "Opening documentation in browser..."
            COMMAND open "${CMAKE_BINARY_DIR}/docs/html/index.html"
            DEPENDS docs
            COMMENT "Opening documentation in default browser"
            VERBATIM
        )
    else()
        # Linux: use xdg-open command
        add_custom_target(docs-open
            COMMAND ${CMAKE_COMMAND} -E echo "Opening documentation in browser..."
            COMMAND xdg-open "${CMAKE_BINARY_DIR}/docs/html/index.html"
            DEPENDS docs
            COMMENT "Opening documentation in default browser"
            VERBATIM
        )
    endif()

    # Documentation installation is handled in cmake/install/Install.cmake
    # This ensures proper platform-specific paths and CPack integration

    message(STATUS "Documentation target ${BoldCyan}'docs'${ColorReset} is available. Build with: ${BoldYellow}cmake --build build --target docs${ColorReset}")
else()
    message(WARNING "${BoldRed}Doxygen${ColorReset} not found. Documentation target will not be available.")
    message(WARNING "Install Doxygen to generate API documentation:")
    message(WARNING "  - macOS: brew install doxygen")
    message(WARNING "  - Ubuntu/Debian: sudo apt-get install doxygen")
    message(WARNING "  - Windows: Download from https://www.doxygen.nl/download.html")

    # Create a dummy target that prints a message
    add_custom_target(docs
        COMMAND ${CMAKE_COMMAND} -E echo "Doxygen not found. Please install Doxygen to generate documentation."
    )

    add_custom_target(docs-open
        COMMAND ${CMAKE_COMMAND} -E echo "Doxygen not found. Please install Doxygen to generate documentation."
    )
endif()

