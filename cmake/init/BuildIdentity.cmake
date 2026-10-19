# CMake can rebuild optimization changes in place, but ABI and dependency
# selections need independent compiler checks, package caches and generated files.
set(_asciichat_identity "${CMAKE_SYSTEM_NAME}|${CMAKE_SYSTEM_PROCESSOR}|${CMAKE_C_COMPILER}|${CMAKE_C_COMPILER_VERSION}|${CMAKE_C_COMPILER_TARGET}|${CMAKE_TOOLCHAIN_FILE}|${VCPKG_TARGET_TRIPLET}|${CMAKE_MSVC_RUNTIME_LIBRARY}")
foreach(_selection USE_MUSL ASCIICHAT_SHARED_DEPS)
    if(${_selection})
        string(APPEND _asciichat_identity "|${_selection}=ON")
    else()
        string(APPEND _asciichat_identity "|${_selection}=OFF")
    endif()
endforeach()
if(DEFINED ASCIICHAT_BUILD_IDENTITY AND NOT ASCIICHAT_BUILD_IDENTITY STREQUAL _asciichat_identity)
    message(FATAL_ERROR
        "This build directory belongs to another toolchain or dependency ABI.\n"
        "Previous: ${ASCIICHAT_BUILD_IDENTITY}\n"
        "Requested: ${_asciichat_identity}\n"
        "Use the matching preset, a separate -B directory, or cmake --fresh.")
endif()
set(ASCIICHAT_BUILD_IDENTITY "${_asciichat_identity}" CACHE INTERNAL "Compiler and dependency ABI for this build tree")
