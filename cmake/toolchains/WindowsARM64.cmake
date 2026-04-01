# Toolchain file for native Windows ARM64 builds with Clang
# Fixes cmake#25466: CMAKE_SYSTEM_PROCESSOR reports AMD64 on ARM64 Windows

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR ARM64)

# Find ARM64 UCRT and MSVC libraries
# Windows Kit UCRT
file(GLOB _winkit_versions "C:/Program Files (x86)/Windows Kits/10/Lib/*")
list(SORT _winkit_versions)
list(GET _winkit_versions -1 _winkit_latest)
set(CMAKE_C_STANDARD_LIBRARIES_INIT "\"${_winkit_latest}/ucrt/arm64/libucrt.lib\"")

# MSVC runtime libraries
file(GLOB _msvc_versions "C:/Program Files/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*")
if(NOT _msvc_versions)
    file(GLOB _msvc_versions "C:/Program Files (x86)/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*")
endif()
list(SORT _msvc_versions)
list(GET _msvc_versions -1 _msvc_latest)

set(CMAKE_C_STANDARD_LIBRARIES_INIT
    "${CMAKE_C_STANDARD_LIBRARIES_INIT} \"${_msvc_latest}/lib/arm64/libvcruntime.lib\" \"${_msvc_latest}/lib/arm64/libcmt.lib\" \"${_msvc_latest}/lib/arm64/legacy_stdio_definitions.lib\"")
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "${CMAKE_C_STANDARD_LIBRARIES_INIT}")
