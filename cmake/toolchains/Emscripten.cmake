# Locate the SDK's toolchain so compiler, platform, and runtime settings stay together.
if(DEFINED ENV{EMSDK})
    set(EMSCRIPTEN_SDK_ROOT "$ENV{EMSDK}/upstream/emscripten")
elseif(DEFINED ENV{EMSCRIPTEN})
    set(EMSCRIPTEN_SDK_ROOT "$ENV{EMSCRIPTEN}")
else()
    set(EMSCRIPTEN_SDK_ROOT "/usr/lib/emscripten")
endif()

find_program(EMCC_EXECUTABLE
    NAMES emcc emcc.bat
    HINTS "${EMSCRIPTEN_SDK_ROOT}"
    REQUIRED
)
get_filename_component(_emscripten_compiler "${EMCC_EXECUTABLE}" REALPATH)
get_filename_component(_emscripten_compiler_dir "${_emscripten_compiler}" DIRECTORY)
find_file(EMSCRIPTEN_PLATFORM_TOOLCHAIN
    NAMES Emscripten.cmake
    HINTS
        "${EMSCRIPTEN_SDK_ROOT}"
        "${_emscripten_compiler_dir}"
        "${_emscripten_compiler_dir}/../libexec"
    PATH_SUFFIXES cmake/Modules/Platform
    NO_DEFAULT_PATH
    REQUIRED
)
include("${EMSCRIPTEN_PLATFORM_TOOLCHAIN}")
