# =============================================================================
# InjectAuthorInfo.cmake
# Injects author information into generated man(3) pages
# Usage: cmake -DMAN_DIR=<dir> -P InjectAuthorInfo.cmake
# =============================================================================

file(TO_CMAKE_PATH "${MAN_DIR}" MAN_DIR)

# Author information to inject
set(AUTHOR_NAME "Zachary Fogg")
set(AUTHOR_EMAIL "me@zfo.gg")

# Find all .3 manpages in the directory
file(GLOB MANPAGES "${MAN_DIR}/*.3")
if(NOT MANPAGES)
    message(STATUS "No manpages found in ${MAN_DIR}, skipping author injection")
    return()
endif()

set(PROCESSED_COUNT 0)

foreach(MANPAGE ${MANPAGES})
    # Read the man page file
    file(READ "${MANPAGE}" CONTENT)

    # Check if Author section exists
    if(CONTENT MATCHES "\\.SH \"Author\"")
        # Replace the generic author text with actual author info
        # Match the pattern: .SH "Author" followed by .PP and generic text
        # Use multiline regex to match across line breaks
        string(REGEX REPLACE
            "(\\.SH \"Author\"\n\\.PP )\nGenerated automatically by Doxygen for ascii-chat from the source code\\\\&\\."
            "\\1\n${AUTHOR_NAME} <${AUTHOR_EMAIL}>"
            CONTENT
            "${CONTENT}")

        # Write the modified content back
        file(WRITE "${MANPAGE}" "${CONTENT}")
        math(EXPR PROCESSED_COUNT "${PROCESSED_COUNT} + 1")
    endif()
endforeach()

message(STATUS "Author injection complete: ${PROCESSED_COUNT} manpages updated with ${AUTHOR_NAME} <${AUTHOR_EMAIL}>")
