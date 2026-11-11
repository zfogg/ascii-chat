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

# The iOS SDK provides getifaddrs through libSystem, but libwebsockets'
# link-based probe cannot resolve it during cross compilation.  Using its
# bundled fallback alongside the SDK's <ifaddrs.h> causes a duplicate struct
# definition, so tell the dependency to use the platform implementation.
set(_file "${LWS_SOURCE_DIR}/CMakeLists.txt")
file(READ "${_file}" _contents)
if(CMAKE_SYSTEM_NAME MATCHES "iOS" AND NOT _contents MATCHES "asciichat iOS getifaddrs")
    string(REPLACE
        "CHECK_FUNCTION_EXISTS(getifaddrs LWS_HAVE_GETIFADDRS)"
        "# asciichat iOS getifaddrs\nif(CMAKE_SYSTEM_NAME MATCHES \"iOS\")\n\tset(LWS_HAVE_GETIFADDRS 1)\nelse()\n\tCHECK_FUNCTION_EXISTS(getifaddrs LWS_HAVE_GETIFADDRS)\nendif()"
        _contents "${_contents}")
    file(WRITE "${_file}" "${_contents}")
endif()

# Wake-on-LAN uses ETHER_ADDR_LEN from desktop networking headers that are not
# available in the iOS SDK.  It is optional and unrelated to websocket or QUIC
# transport, so leave it out of the iOS static library.
if(CMAKE_SYSTEM_NAME MATCHES "iOS" AND NOT _contents MATCHES "asciichat iOS WOL")
    string(REPLACE
        "project(libwebsockets C)"
        "project(libwebsockets C)\n# asciichat iOS WOL\nset(LWS_WITH_WOL OFF CACHE BOOL \"Disable Wake-on-LAN on iOS\" FORCE)"
        _contents "${_contents}")
    file(WRITE "${_file}" "${_contents}")
endif()

set(_file "${LWS_SOURCE_DIR}/lib/CMakeLists.txt")
file(READ "${_file}" _contents)
set(_original "${_contents}")
# Staged TLS headers belong to the build tree, not an installed package interface.
string(REPLACE "\"$<BUILD_INTERFACE:\${inc}>\" \"$<INSTALL_INTERFACE:\${inc}>\""
    "\"$<BUILD_INTERFACE:\${inc}>\"" _contents "${_contents}")
if(NOT _contents STREQUAL _original)
    file(WRITE "${_file}" "${_contents}")
endif()
