set(_file "${LIBDATACHANNEL_SOURCE_DIR}/deps/usrsctp/CMakeLists.txt")
file(READ "${_file}" _contents)
set(_original "${_contents}")
# MSVC ABI compatibility does not imply an MSVC-style compiler command line.
string(REPLACE "# SETTINGS FOR VISUAL STUDIO COMPILER\nif (C_COMPILER_IS_MSVC_LIKE)"
    "# SETTINGS FOR VISUAL STUDIO COMPILER\nif (CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL \"MSVC\")"
    _contents "${_contents}")
if(NOT _contents STREQUAL _original)
    file(WRITE "${_file}" "${_contents}")
endif()

set(_file "${LIBDATACHANNEL_SOURCE_DIR}/src/configuration.cpp")
file(READ "${_file}" _contents)
# libc++ no longer includes the algorithm declarations through other headers.
if(NOT _contents MATCHES "#include <algorithm>")
    file(WRITE "${_file}" "#include <algorithm>\n${_contents}")
endif()
