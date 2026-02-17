set(CMAKE_SYSTEM_NAME Emscripten)

# Detect Emscripten SDK location from environment
if(DEFINED ENV{EMSDK})
    set(EMSCRIPTEN_SDK_ROOT "$ENV{EMSDK}/upstream/emscripten")
elseif(DEFINED ENV{EMSCRIPTEN})
    set(EMSCRIPTEN_SDK_ROOT "$ENV{EMSCRIPTEN}")
else()
    # Try common installation paths
    set(EMSCRIPTEN_SDK_ROOT "/usr/lib/emscripten")
endif()

# Resolve emcc/em++ from Emscripten SDK or PATH
# emcmake sets CC/CXX environment variables, but we need to ensure we're using the right emcc
if(DEFINED ENV{CC})
    set(CMAKE_C_COMPILER "$ENV{CC}")
else()
    find_program(EMCC_EXECUTABLE NAMES emcc PATHS "${EMSCRIPTEN_SDK_ROOT}" NO_DEFAULT_PATH)
    if(EMCC_EXECUTABLE)
        set(CMAKE_C_COMPILER "${EMCC_EXECUTABLE}")
    else()
        set(CMAKE_C_COMPILER emcc)
    endif()
endif()

if(DEFINED ENV{CXX})
    set(CMAKE_CXX_COMPILER "$ENV{CXX}")
else()
    find_program(EMXX_EXECUTABLE NAMES em++ PATHS "${EMSCRIPTEN_SDK_ROOT}" NO_DEFAULT_PATH)
    if(EMXX_EXECUTABLE)
        set(CMAKE_CXX_COMPILER "${EMXX_EXECUTABLE}")
    else()
        set(CMAKE_CXX_COMPILER em++)
    endif()
endif()

# Emscripten-specific linker
if(DEFINED ENV{AR})
    set(CMAKE_AR "$ENV{AR}")
else()
    find_program(EMAR_EXECUTABLE NAMES emar PATHS "${EMSCRIPTEN_SDK_ROOT}" NO_DEFAULT_PATH)
    if(EMAR_EXECUTABLE)
        set(CMAKE_AR "${EMAR_EXECUTABLE}")
    else()
        set(CMAKE_AR emar)
    endif()
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
