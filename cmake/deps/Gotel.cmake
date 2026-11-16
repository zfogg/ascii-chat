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
# Keep the optional gRPC adapter disabled in the application build. The
# pinned gotel/otlp-c sources expose overlapping HTTP request symbols when
# combined with the application's HTTP exporter on Windows; telemetry uses
# OTLP/HTTP here and nghttp2 remains available to the native HTTP server.
# Keep Gotel's optional gRPC telemetry transport disabled.  Its pinned adapter
# rewrites the HTTP source staging path and is not needed by ascii-chat's HTTP
#/2 server.
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

# Keep the vendored C++ facade aligned with ascii-chat's C++26 toolchain.
set(GOTEL_CXX_STANDARD 26 CACHE STRING "C++ standard for the gotel facade" FORCE)

# Keep Gotel and its fetched otlp-c/Sequel build trees in the persistent
# dependency cache. This is separate from the main build directory.
set(_asciichat_gotel_cache_dir "${CMAKE_BINARY_DIR}/_deps/gotel")
file(MAKE_DIRECTORY "${_asciichat_gotel_cache_dir}")

set(_asciichat_parent_shared_libs ${BUILD_SHARED_LIBS})
if(USE_MUSL OR ASCIICHAT_STATIC_DEPS)
    set(_asciichat_gotel_shared OFF)
else()
    set(_asciichat_gotel_shared ${ASCIICHAT_SHARED_DEPS})
endif()
set(BUILD_SHARED_LIBS ${_asciichat_gotel_shared})
set(BUILD_SHARED_LIBS ${_asciichat_gotel_shared} CACHE BOOL
    "Build gotel and its public dependencies as shared libraries" FORCE)

# Gotel's gRPC adapter accepts an existing nghttp2 target. Prefer the target
# supplied by vcpkg so its embedded FetchContent project does not define a
# second target with the same name.
if(GOTEL_NGHTTP2_TARGET AND NOT TARGET "${GOTEL_NGHTTP2_TARGET}")
    unset(GOTEL_NGHTTP2_TARGET CACHE)
endif()
if(TARGET nghttp2::nghttp2)
    set(GOTEL_NGHTTP2_TARGET nghttp2::nghttp2 CACHE STRING
        "Use the vcpkg nghttp2 target for gotel gRPC" FORCE)
elseif(TARGET nghttp2)
    set(GOTEL_NGHTTP2_TARGET nghttp2 CACHE STRING
        "Use the vcpkg nghttp2 target for gotel gRPC" FORCE)
elseif(TARGET nghttp2_static)
    set(GOTEL_NGHTTP2_TARGET nghttp2_static CACHE STRING
        "Use the existing nghttp2 target for gotel gRPC" FORCE)
endif()

if(NOT GOTEL_NGHTTP2_TARGET)
    set(_asciichat_nghttp2_vcpkg_root
        "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")
    if(NOT EXISTS "${_asciichat_nghttp2_vcpkg_root}")
        set(_asciichat_nghttp2_vcpkg_root
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    endif()
    unset(_asciichat_nghttp2_include_dir CACHE)
    unset(_asciichat_nghttp2_release_library CACHE)
    unset(_asciichat_nghttp2_debug_library CACHE)
    find_path(_asciichat_nghttp2_include_dir
        NAMES nghttp2/nghttp2.h
        PATHS "${_asciichat_nghttp2_vcpkg_root}/include"
        NO_DEFAULT_PATH)
    find_library(_asciichat_nghttp2_release_library
        NAMES nghttp2
        PATHS "${_asciichat_nghttp2_vcpkg_root}/lib"
        NO_DEFAULT_PATH)
    find_library(_asciichat_nghttp2_debug_library
        NAMES nghttp2 nghttp2d
        PATHS "${_asciichat_nghttp2_vcpkg_root}/debug/lib"
        NO_DEFAULT_PATH)

    if(_asciichat_nghttp2_include_dir AND
       _asciichat_nghttp2_release_library AND
       _asciichat_nghttp2_debug_library)
        add_library(asciichat-vcpkg-nghttp2 UNKNOWN IMPORTED GLOBAL)
        set_target_properties(asciichat-vcpkg-nghttp2 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_asciichat_nghttp2_include_dir}"
            IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
            MAP_IMPORTED_CONFIG_DEV DEBUG
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
            IMPORTED_LOCATION_DEBUG "${_asciichat_nghttp2_debug_library}"
            IMPORTED_LOCATION_RELEASE "${_asciichat_nghttp2_release_library}"
            IMPORTED_IMPLIB_DEBUG "${_asciichat_nghttp2_debug_library}"
            IMPORTED_IMPLIB_RELEASE "${_asciichat_nghttp2_release_library}")
        set(GOTEL_NGHTTP2_TARGET asciichat-vcpkg-nghttp2 CACHE STRING
            "Use the vcpkg nghttp2 target for gotel gRPC" FORCE)
    endif()
endif()

# These dependency revisions are pinned in the vendored Gotel checkout.  Keep
# FetchContent from running its git update step on every configure: updating a
# populated git checkout refreshes all source mtimes, which makes Ninja treat
# the entire gotel/nghttp2/otlp-c dependency tree as stale on the next build.
# First-time population still occurs when a source directory is absent.  Do
# not force FETCHCONTENT_FULLY_DISCONNECTED here: a cold CI dependency cache
# must be allowed to download the pinned sources.
set(_asciichat_parent_fetchcontent_updates_disconnected
    ${FETCHCONTENT_UPDATES_DISCONNECTED})
set(_asciichat_parent_fetchcontent_fully_disconnected
    ${FETCHCONTENT_FULLY_DISCONNECTED})
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
set(FETCHCONTENT_FULLY_DISCONNECTED OFF)
set(_asciichat_fetchcontent_base_dir_was_defined FALSE)
if(DEFINED FETCHCONTENT_BASE_DIR)
    set(_asciichat_fetchcontent_base_dir_was_defined TRUE)
    set(_asciichat_parent_fetchcontent_base_dir "${FETCHCONTENT_BASE_DIR}")
endif()
set(FETCHCONTENT_BASE_DIR "${_asciichat_gotel_cache_dir}" CACHE PATH
    "Persistent Gotel dependency source/build cache" FORCE)

if(USE_MUSL AND NOT TARGET nghttp2_static)
    # The musl application links its native HTTP/2 server against a static
    # nghttp2 target. Build that target directly instead of enabling Gotel's
    # unrelated gRPC adapter just to make it appear.
    set(ENABLE_LIB_ONLY ON CACHE BOOL "Build nghttp2 library only" FORCE)
    set(BUILD_STATIC_LIBS ON CACHE BOOL "Build nghttp2 static libraries" FORCE)
    set(ENABLE_HTTP3 OFF CACHE BOOL "Disable nghttp2 HTTP/3 integration" FORCE)
    set(ENABLE_APP OFF CACHE BOOL "Disable nghttp2 applications" FORCE)
    set(ENABLE_HPACK_TOOLS OFF CACHE BOOL "Disable nghttp2 HPACK tools" FORCE)
    set(ENABLE_EXAMPLES OFF CACHE BOOL "Disable nghttp2 examples" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "Disable nghttp2 tests" FORCE)
    FetchContent_Declare(asciichat_nghttp2
        GIT_REPOSITORY https://github.com/nghttp2/nghttp2.git
        GIT_TAG 43b29f991403b5419dd0bea2840581078ead9bf8)
    FetchContent_MakeAvailable(asciichat_nghttp2)
    if(TARGET nghttp2_static)
        set_target_properties(nghttp2_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
endif()

add_subdirectory(${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/gotel/c
                 "${_asciichat_gotel_cache_dir}/build"
                 EXCLUDE_FROM_ALL)

# The vendored Gotel shared target calls its TLS selection helper directly,
# while some Apple linkers do not export that helper from the separately built
# otlp-c dylib.  Keep the helper implementation in Gotel as well so the
# application does not depend on dylib export behavior for this internal API.
if(APPLE AND TARGET gotel)
    target_sources(gotel PRIVATE
        "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/gotel/c/src/tls.c")
    target_link_libraries(gotel PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

# gotel's gRPC adapter uses one private URL bit to distinguish its HTTP/2
# transport. Keep that integration detail in the generated bridge header; do
# not modify the pinned otlp-c checkout or require a commit in that repository.
set(_asciichat_otlp_http_header
    "${_asciichat_gotel_cache_dir}/build/otlp-tls/http_client.h")
if(EXISTS "${_asciichat_otlp_http_header}")
    file(READ "${_asciichat_otlp_http_header}" _asciichat_otlp_http_text)
    if(NOT _asciichat_otlp_http_text MATCHES "int grpc;")
        string(REPLACE "int tls;" "int tls;\n int grpc;"
            _asciichat_otlp_http_text "${_asciichat_otlp_http_text}")
        file(READ "${_asciichat_otlp_http_header}" _asciichat_otlp_http_existing)
        if(NOT _asciichat_otlp_http_existing STREQUAL _asciichat_otlp_http_text)
            file(WRITE "${_asciichat_otlp_http_header}" "${_asciichat_otlp_http_text}")
        endif()
        unset(_asciichat_otlp_http_existing)
    endif()
endif()

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
set(FETCHCONTENT_UPDATES_DISCONNECTED
    ${_asciichat_parent_fetchcontent_updates_disconnected})
set(FETCHCONTENT_FULLY_DISCONNECTED
    ${_asciichat_parent_fetchcontent_fully_disconnected})
unset(_asciichat_parent_fetchcontent_base_dir)
unset(_asciichat_fetchcontent_base_dir_was_defined)
unset(_asciichat_parent_fetchcontent_updates_disconnected)
unset(_asciichat_parent_fetchcontent_fully_disconnected)
add_compile_definitions(GOTEL_WITH_SQLITE=1)
include_directories($<TARGET_PROPERTY:gotel,INTERFACE_INCLUDE_DIRECTORIES>
                    $<TARGET_PROPERTY:otlp_c,INTERFACE_INCLUDE_DIRECTORIES>)
