set(_file "${PORTAUDIO_SOURCE_DIR}/CMakeLists.txt")
file(READ "${_file}" _contents)
set(_original "${_contents}")
# The GNU-style Clang driver still uses the Windows SDK, including Vista audio APIs.
string(REPLACE "_WIN32_WINNT=0x0501 WINVER=0x0501"
    "_WIN32_WINNT=0x0601 WINVER=0x0601" _contents "${_contents}")
if(NOT _contents STREQUAL _original)
    file(WRITE "${_file}" "${_contents}")
endif()
