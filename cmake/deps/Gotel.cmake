# =============================================================================
# Gotel / OTLP-C telemetry dependency
# =============================================================================

include_guard(GLOBAL)

if(EMSCRIPTEN)
    return()
endif()

set(GOTEL_BUILD_TEST_SUPPORT ${BUILD_TESTS} CACHE BOOL
    "Build reusable telemetry test helpers" FORCE)
set(GOTEL_WITH_SQLITE ON CACHE BOOL "Build SQLite telemetry adapter" FORCE)
set(GOTEL_WITH_GRPC ON CACHE BOOL "Build native OTLP/gRPC telemetry transport" FORCE)
set(GOTEL_UTF8PROC_TARGET utf8proc CACHE STRING
    "Use ascii-chat's utf8proc target" FORCE)

add_library(asciichat-gotel-pcre2 INTERFACE)
target_include_directories(asciichat-gotel-pcre2 INTERFACE ${PCRE2_INCLUDE_DIRS})
target_link_libraries(asciichat-gotel-pcre2 INTERFACE ${PCRE2_LIBRARIES})
set(GOTEL_PCRE2_TARGET asciichat-gotel-pcre2 CACHE STRING
    "Use ascii-chat's PCRE2 dependency" FORCE)

add_library(asciichat-gotel-sqlite INTERFACE)
if(SQLITE3_INCLUDE_DIRS)
    target_include_directories(asciichat-gotel-sqlite INTERFACE ${SQLITE3_INCLUDE_DIRS})
endif()
target_link_libraries(asciichat-gotel-sqlite INTERFACE ${SQLITE3_LIBRARIES})
set(GOTEL_SQLITE_TARGET asciichat-gotel-sqlite CACHE STRING
    "Use ascii-chat's SQLite3 dependency" FORCE)

# Keep the vendored C++ facade aligned with the selected compiler's C++ mode.
if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    set(GOTEL_CXX_STANDARD 23 CACHE STRING "C++ standard for the gotel facade" FORCE)
else()
    set(GOTEL_CXX_STANDARD 26 CACHE STRING "C++ standard for the gotel facade" FORCE)
endif()

# Keep Gotel and its fetched otlp-c/Sequel build trees in the persistent
# dependency cache. This is separate from the main build directory.
set(_asciichat_gotel_cache_dir "${ASCIICHAT_DEPS_CACHE_DIR}/gotel")
file(MAKE_DIRECTORY "${_asciichat_gotel_cache_dir}")

set(_asciichat_parent_shared_libs ${BUILD_SHARED_LIBS})
# Musl cannot safely consume host/shared dependency builds. Otherwise honor
# ASCIICHAT_SHARED_DEPS: static remains the default, including Release, while
# ASCIICHAT_SHARED_DEPS=ON explicitly selects shared dependencies.
if(USE_MUSL)
    set(_asciichat_gotel_shared OFF)
else()
    set(_asciichat_gotel_shared ${ASCIICHAT_SHARED_DEPS})
endif()
set(BUILD_SHARED_LIBS ${_asciichat_gotel_shared})
set(FETCHCONTENT_BASE_DIR "${_asciichat_gotel_cache_dir}" CACHE PATH
    "Persistent Gotel dependency source/build cache" FORCE)

add_subdirectory(${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/gotel/c
                 "${_asciichat_gotel_cache_dir}/build")

# gotel's util/utf8.c includes util/utf8.h relative to the C source root;
# explicitly expose that root for compilers that do not add it implicitly.
if(TARGET gotel)
    target_include_directories(gotel PRIVATE
        "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/gotel/c/src")
endif()

if(USE_MUSL AND TARGET gotel_cxx AND ALPINE_LIBCXX_INCLUDE_DIR)
    target_include_directories(gotel_cxx SYSTEM PRIVATE "${ALPINE_LIBCXX_INCLUDE_DIR}")
endif()

if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    foreach(_target gotel otlp_c sequel)
        if(TARGET ${_target})
            target_compile_definitions(${_target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
    endforeach()
    if(TARGET otlp_c)
        target_compile_options(otlp_c PRIVATE -Wno-sign-conversion)
    endif()
    if(TARGET nghttp2_static)
        target_compile_options(nghttp2_static PRIVATE -Wno-shorten-64-to-32)
    endif()
endif()

# Keep the pinned exporter's serialized batch immutable while HTTP owns it.
# Otherwise the next tick appends records that clear_in_flight_batch frees unsent.
get_target_property(_otel_source_root otlp_c SOURCE_DIR)
file(READ "${_otel_source_root}/src/exporter.c" _otel_exporter)
set(_otel_drain "\t\t\twhile (e->sig[s].pending_count < e->sig[s].pending_cap)")
string(FIND "${_otel_exporter}" "${_otel_drain}" _otel_drain_offset)
if(_otel_drain_offset EQUAL -1)
    message(FATAL_ERROR "Pinned otlp-c exporter queue implementation changed")
endif()
string(REPLACE "${_otel_drain}"
    "\t\t\tif (e->in_flight && e->in_flight_signal == s)\n\t\t\t\tcontinue;\n${_otel_drain}"
    _otel_exporter "${_otel_exporter}")
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/otlp_exporter.c"
    CONTENT "${_otel_exporter}" @ONLY)
get_target_property(_otel_sources otlp_c SOURCES)
list(FILTER _otel_sources EXCLUDE REGEX "(^|/)exporter\\.c$")
set_property(TARGET otlp_c PROPERTY SOURCES "${_otel_sources}")
target_sources(otlp_c PRIVATE "${CMAKE_BINARY_DIR}/generated/otlp_exporter.c")

set(BUILD_SHARED_LIBS ${_asciichat_parent_shared_libs})
add_compile_definitions(GOTEL_WITH_SQLITE=1)
include_directories($<TARGET_PROPERTY:gotel,INTERFACE_INCLUDE_DIRECTORIES>
                    $<TARGET_PROPERTY:otlp_c,INTERFACE_INCLUDE_DIRECTORIES>)
