# Compatibility wrapper. The shared implementation lives in the cmake-utils
# submodule so all zfogg projects use the same cache-launcher behavior.
include(${CMAKE_SOURCE_DIR}/deps/cmake-utils/CacheLauncher.cmake)
cmake_utils_configure_cache_launcher()
