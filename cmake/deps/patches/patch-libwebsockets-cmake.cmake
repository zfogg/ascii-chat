set(_file "${LWS_SOURCE_DIR}/CMakeLists.txt")
file(READ "${_file}" _contents)
set(_original "${_contents}")
# zlib is built later in the same graph. TLS/platform probes only need their own libraries.
string(REPLACE "set(CMAKE_REQUIRED_LIBRARIES \${CMAKE_REQUIRED_LIBRARIES} \${ZLIB_LIBRARIES})"
    "" _contents "${_contents}")
if(NOT _contents STREQUAL _original)
    file(WRITE "${_file}" "${_contents}")
endif()
foreach(_relative CMakeLists.txt lib/CMakeLists.txt lib/tls/CMakeLists.txt)
    set(_file "${LWS_SOURCE_DIR}/${_relative}")
    file(READ "${_file}" _contents)
    if(NOT _contents MATCHES "asciichat target probes")
        string(REGEX REPLACE "(set\\(CMAKE_REQUIRED_LIBRARIES[^\n]*\\))"
            "\\1\n# asciichat target probes exclude the not-yet-built compression library.\nlist(REMOVE_ITEM CMAKE_REQUIRED_LIBRARIES \${ZLIB_LIBRARIES} asciichat-zlib ZLIB::ZLIB)"
            _contents "${_contents}")
        file(WRITE "${_file}" "${_contents}")
    endif()
endforeach()

set(_file "${LWS_SOURCE_DIR}/lib/CMakeLists.txt")
file(READ "${_file}" _contents)
set(_original "${_contents}")
# Staged TLS headers belong to the build tree, not an installed package interface.
string(REPLACE "\"$<BUILD_INTERFACE:\${inc}>\" \"$<INSTALL_INTERFACE:\${inc}>\""
    "\"$<BUILD_INTERFACE:\${inc}>\"" _contents "${_contents}")
if(NOT _contents STREQUAL _original)
    file(WRITE "${_file}" "${_contents}")
endif()
