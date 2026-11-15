# =============================================================================
# Gotel / OTLP-C telemetry dependency
# =============================================================================

include_guard(GLOBAL)

if(EMSCRIPTEN)
    return()
endif()

set(GOTEL_BUILD_TEST_SUPPORT ${BUILD_TESTS} CACHE BOOL
    "Build reusable telemetry test helpers" FORCE)
if(USE_MUSL)
    # The static musl application links nghttp2_static directly.  The master
    # gotel default disables its gRPC adapter, which would leave that required
    # target undefined; keep the adapter enabled only for this build contract.
    set(GOTEL_WITH_GRPC ON CACHE BOOL
        "Build native OTLP/gRPC telemetry transport" FORCE)
endif()
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
set(_asciichat_build_shared_libs_cache_defined FALSE)
if(DEFINED BUILD_SHARED_LIBS)
    get_property(_asciichat_build_shared_libs_cache_defined CACHE BUILD_SHARED_LIBS PROPERTY TYPE SET)
    if(_asciichat_build_shared_libs_cache_defined)
        set(_asciichat_parent_build_shared_libs_cache "${BUILD_SHARED_LIBS}")
    endif()
endif()
set(_asciichat_fetchcontent_base_dir_was_defined FALSE)
if(DEFINED FETCHCONTENT_BASE_DIR)
    set(_asciichat_fetchcontent_base_dir_was_defined TRUE)
    set(_asciichat_parent_fetchcontent_base_dir "${FETCHCONTENT_BASE_DIR}")
endif()
set(FETCHCONTENT_BASE_DIR "${_asciichat_gotel_cache_dir}" CACHE PATH
    "Persistent Gotel dependency source/build cache" FORCE)

# otlp-c's protobuf and exporter helpers are internal implementation symbols
# used by gotel-testing. They are not exported by a shared otlp-c library, so
# always build this dependency statically even when ascii-chat uses shared
# dependency libraries. Force the cache value as well as the normal variable:
# the persistent dependency build cache may otherwise retain ON from an older
# configure.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
set(BUILD_SHARED_LIBS OFF)

# Gotel's vendored utf8proc CMake project predates the shared dependency target
# used by ascii-chat and unconditionally creates a second target named
# `utf8proc`. Guard that dependency at configure time so Gotel reuses the
# already-configured target on every compiler family.
set(_asciichat_gotel_cmake "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/gotel/c/CMakeLists.txt")
if(EXISTS "${_asciichat_gotel_cmake}")
    file(READ "${_asciichat_gotel_cmake}" _asciichat_gotel_cmake_contents)
    # Patch the upstream add_subdirectory exactly once. Repeated configure
    # runs must not nest another guard into the vendored checkout.
    string(FIND "${_asciichat_gotel_cmake_contents}"
        "if(NOT TARGET utf8proc AND NOT TARGET utf8proc::utf8proc)"
        _asciichat_gotel_guard_offset)
    string(FIND "${_asciichat_gotel_cmake_contents}"
        "add_subdirectory(vendor/utf8proc EXCLUDE_FROM_ALL)"
        _asciichat_gotel_add_offset)
    if(_asciichat_gotel_guard_offset EQUAL -1 AND NOT _asciichat_gotel_add_offset EQUAL -1)
        set(_asciichat_gotel_utf8proc_add
            "add_subdirectory(vendor/utf8proc EXCLUDE_FROM_ALL)")
        set(_asciichat_gotel_utf8proc_guard [=[if(NOT TARGET utf8proc AND NOT TARGET utf8proc::utf8proc)
  add_subdirectory(vendor/utf8proc EXCLUDE_FROM_ALL)
endif()]=])
        string(REPLACE "${_asciichat_gotel_utf8proc_add}"
            "${_asciichat_gotel_utf8proc_guard}"
            _asciichat_gotel_cmake_patched "${_asciichat_gotel_cmake_contents}")
    else()
        set(_asciichat_gotel_cmake_patched "${_asciichat_gotel_cmake_contents}")
    endif()
    if(NOT _asciichat_gotel_cmake_patched STREQUAL _asciichat_gotel_cmake_contents)
        file(WRITE "${_asciichat_gotel_cmake}" "${_asciichat_gotel_cmake_patched}")
    endif()
endif()

# gotel master briefly renamed the TLS-staged http_client.c to http1_client.c
# before its GRPC adapter reads the former path.  Keep the master source and
# apply the compatibility correction in the consumer checkout, idempotently.
set(_asciichat_gotel_tls_cmake "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/gotel/c/cmake/TLS.cmake")
if(EXISTS "${_asciichat_gotel_tls_cmake}")
    file(READ "${_asciichat_gotel_tls_cmake}" _asciichat_gotel_tls_contents)
    set(_asciichat_gotel_tls_rename
        "  if(_name STREQUAL \"http_client.c\" AND GOTEL_WITH_GRPC)\n    set(_output_name \"http1_client.c\")\n  endif()\n")
    string(REPLACE "${_asciichat_gotel_tls_rename}" ""
        _asciichat_gotel_tls_patched "${_asciichat_gotel_tls_contents}")
    if(NOT _asciichat_gotel_tls_patched STREQUAL _asciichat_gotel_tls_contents)
        file(WRITE "${_asciichat_gotel_tls_cmake}" "${_asciichat_gotel_tls_patched}")
    endif()
endif()

add_subdirectory(${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/gotel/c
                 "${_asciichat_gotel_cache_dir}/build")

if(_asciichat_build_shared_libs_cache_defined)
    set(BUILD_SHARED_LIBS "${_asciichat_parent_build_shared_libs_cache}" CACHE BOOL
        "Build shared libraries" FORCE)
else()
    unset(BUILD_SHARED_LIBS CACHE)
endif()
set(BUILD_SHARED_LIBS ${_asciichat_gotel_shared})

# gotel's util/utf8.c includes util/utf8.h relative to the C source root;
# explicitly expose that root for compilers that do not add it implicitly.
if(_asciichat_fetchcontent_base_dir_was_defined)
    set(FETCHCONTENT_BASE_DIR "${_asciichat_parent_fetchcontent_base_dir}")
    set(FETCHCONTENT_BASE_DIR "${_asciichat_parent_fetchcontent_base_dir}" CACHE PATH
        "FetchContent source cache" FORCE)
else()
    unset(FETCHCONTENT_BASE_DIR)
    unset(FETCHCONTENT_BASE_DIR CACHE)
endif()

# gotel's vendored CMake target supplies utf8proc for both gotel and the
# application.  Configure it after gotel so ascii-chat does not create a
# second target with the same name before gotel is added.
set(UTF8PROC_LIBRARIES utf8proc CACHE STRING "utf8proc target supplied by gotel" FORCE)
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
set(_otel_flush_shutdown "\tif (otlp_atomic_load_int(&e->shutdown_requested, OTLP_MEMORY_ORDER_ACQUIRE)) {\n\t\tif (e->in_flight) {\n\t\t\totlp_http_request_free(e->in_flight);\n\t\t\te->in_flight = NULL;\n\t\t\te->in_flight_count = 0;\n\t\t}\n\t\tfor (int s = 0; s < 3; ++s)\n\t\t\tdrain_signal(&e->sig[s], SIGNAL_SPECS[s].free_item);\n\t\treturn OTLP_OK;\n\t}\n")
string(REPLACE "\tif (!e)\n\t\treturn OTLP_ERR_NULL;\n\tdeadline = otlp_platform_now_mono_ms() + e->flush_timeout_ms;" "\tif (!e)\n\t\treturn OTLP_ERR_NULL;\n${_otel_flush_shutdown}\tdeadline = otlp_platform_now_mono_ms() + e->flush_timeout_ms;" _otel_exporter "${_otel_exporter}")
set(_otel_drain "\t\t\twhile (e->sig[s].pending_count < e->sig[s].pending_cap)")
string(FIND "${_otel_exporter}" "${_otel_drain}" _otel_drain_offset)
if(_otel_drain_offset EQUAL -1)
    message(FATAL_ERROR "Pinned otlp-c exporter queue implementation changed")
endif()
string(REPLACE "${_otel_drain}"
    "\t\t\tif (e->in_flight && e->in_flight_signal == s)\n\t\t\t\tcontinue;\n${_otel_drain}"
    _otel_exporter "${_otel_exporter}")
# A shutdown request must also cancel an already in-flight HTTP request. The
# exporter tick loop is the single owner of that request, so freeing it here
# is safe and lets gotel_drain() observe an empty queue on its next iteration.
set(_otel_inflight_step "\t\t\totlp_status_t st = otlp_http_request_step(e->in_flight);")
string(FIND "${_otel_exporter}" "${_otel_inflight_step}" _otel_inflight_offset)
if(_otel_inflight_offset EQUAL -1)
    message(FATAL_ERROR "Pinned otlp-c exporter in-flight request implementation changed")
endif()
string(REPLACE "${_otel_inflight_step}"
    "\t\t\tif (otlp_atomic_load_int(&e->shutdown_requested, OTLP_MEMORY_ORDER_ACQUIRE)) {\n\t\t\t\totlp_http_request_free(e->in_flight);\n\t\t\t\te->in_flight = NULL;\n\t\t\t\te->in_flight_count = 0;\n\t\t\t\twork_done = true;\n\t\t\t\tcontinue;\n\t\t\t}\n${_otel_inflight_step}"
    _otel_exporter "${_otel_exporter}")
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/otlp_exporter.c"
    CONTENT "${_otel_exporter}" @ONLY)
get_target_property(_otel_sources otlp_c SOURCES)
list(FILTER _otel_sources EXCLUDE REGEX "(^|/)exporter\\.c$")
set_property(TARGET otlp_c PROPERTY SOURCES "${_otel_sources}")
target_sources(otlp_c PRIVATE "${CMAKE_BINARY_DIR}/generated/otlp_exporter.c")

set(BUILD_SHARED_LIBS ${_asciichat_parent_shared_libs})
unset(_asciichat_parent_fetchcontent_base_dir)
unset(_asciichat_fetchcontent_base_dir_was_defined)
unset(_asciichat_parent_build_shared_libs_cache)
unset(_asciichat_build_shared_libs_cache_defined)
add_compile_definitions(GOTEL_WITH_SQLITE=1)
include_directories($<TARGET_PROPERTY:gotel,INTERFACE_INCLUDE_DIRECTORIES>
                    $<TARGET_PROPERTY:otlp_c,INTERFACE_INCLUDE_DIRECTORIES>)
