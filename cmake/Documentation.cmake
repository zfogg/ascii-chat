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
    message(STATUS "Doxygen found: ${DOXYGEN_EXECUTABLE}")

    # Configure Doxyfile from template
    set(DOXYFILE_IN "${CMAKE_SOURCE_DIR}/Doxyfile.in")
    set(DOXYFILE_OUT "${CMAKE_BINARY_DIR}/Doxyfile")

    configure_file(
        ${DOXYFILE_IN}
        ${DOXYFILE_OUT}
        @ONLY
    )

    # Create docs directory if it doesn't exist
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/docs")

    # Create documentation target
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
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

    # Install documentation (optional)
    install(
        DIRECTORY ${CMAKE_BINARY_DIR}/docs/html
        DESTINATION share/doc/ascii-chat
        OPTIONAL
    )

    message(STATUS "Documentation target 'docs' is available. Build with: cmake --build build --target docs")
else()
    message(WARNING "Doxygen not found. Documentation target will not be available.")
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

