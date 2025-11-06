# =============================================================================
# Documentation Module
# =============================================================================
# This module creates targets for generating Doxygen API documentation
#
# Prerequisites:
#   - Doxygen executable must be installed
#
# Outputs:
#   - docs target for building documentation
#   - Documentation will be generated in ${CMAKE_BINARY_DIR}/docs/html/
# =============================================================================

# Find Doxygen executable
find_program(DOXYGEN_EXECUTABLE NAMES doxygen)

if(DOXYGEN_EXECUTABLE)
    message(STATUS "Found ${BoldGreen}Doxygen${ColorReset}: ${DOXYGEN_EXECUTABLE}")

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
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
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

