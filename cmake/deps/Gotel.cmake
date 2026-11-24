# =============================================================================
# Gotel / OTLP-C telemetry dependency
# =============================================================================

include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)

if(EMSCRIPTEN)
    return()
endif()

set(GOTEL_BUILD_TEST_SUPPORT ${BUILD_TESTS} CACHE BOOL
    "Build reusable telemetry test helpers" FORCE)
set(GOTEL_WITH_SQLITE ON CACHE BOOL "Build SQLite telemetry adapter" FORCE)
# gRPC is part of the application's telemetry contract and must be built on
# every native configuration.
set(GOTEL_WITH_GRPC ON CACHE BOOL "Build native OTLP/gRPC telemetry transport" FORCE)
set(GOTEL_UTF8PROC_TARGET utf8proc CACHE STRING
    "Use ascii-chat's utf8proc target" FORCE)

add_library(asciichat-gotel-pcre2 INTERFACE)
target_link_libraries(asciichat-gotel-pcre2 INTERFACE asciichat-pcre2)
set(GOTEL_PCRE2_TARGET asciichat-gotel-pcre2 CACHE STRING
    "Use ascii-chat's PCRE2 dependency" FORCE)

add_library(asciichat-gotel-sqlite INTERFACE)
target_link_libraries(asciichat-gotel-sqlite INTERFACE asciichat-sqlite3)
set(GOTEL_SQLITE_TARGET asciichat-gotel-sqlite CACHE STRING
    "Use ascii-chat's SQLite3 dependency" FORCE)

# Keep the vendored C++ facade aligned with ascii-chat's C++26 toolchain.
if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    set(GOTEL_CXX_STANDARD 23 CACHE STRING "C++ standard for the gotel facade" FORCE)
else()
    set(GOTEL_CXX_STANDARD 26 CACHE STRING "C++ standard for the gotel facade" FORCE)
endif()

# Gotel declares Sequel in its nested CMake project. Predeclare the persistent
# cache checkout here so reconfiguring another parent build directory does not
# clone Sequel again.
include(FetchContent)
string(REGEX REPLACE "[^A-Za-z0-9_.-]" "-" _asciichat_gotel_generator_cache "${CMAKE_GENERATOR}")
string(REGEX REPLACE "[^A-Za-z0-9_.-]" "-" _asciichat_gotel_compiler_cache "${CMAKE_C_COMPILER_ID}")
if(NOT _asciichat_gotel_compiler_cache)
    set(_asciichat_gotel_compiler_cache default)
endif()
set(_asciichat_gotel_cache_dir
    "${ASCIICHAT_DEPS_CACHE_DIR}/gotel-${_asciichat_gotel_compiler_cache}-${_asciichat_gotel_generator_cache}")
file(MAKE_DIRECTORY "${_asciichat_gotel_cache_dir}")
set(_asciichat_sequel_source "${ASCIICHAT_DEPS_CACHE_DIR}/sequel-src")
if(EXISTS "${_asciichat_sequel_source}/CMakeLists.txt")
    asciichat_fetchcontent_declare(sequel
        SOURCE_DIR "${_asciichat_sequel_source}"
        BINARY_DIR "${_asciichat_gotel_cache_dir}/sequel-build")
else()
    asciichat_fetchcontent_declare(sequel
        GIT_REPOSITORY https://github.com/zfogg/sequel.git
        GIT_TAG f0865335e0c90b872ad2d4b40d2590509439f1fe
        SOURCE_DIR "${_asciichat_sequel_source}"
        BINARY_DIR "${_asciichat_gotel_cache_dir}/sequel-build")
endif()
set(_asciichat_otlp_c_source "${ASCIICHAT_DEPS_CACHE_DIR}/otlp_c-src")
if(EXISTS "${_asciichat_otlp_c_source}/CMakeLists.txt")
    asciichat_fetchcontent_declare(otlp_c
        SOURCE_DIR "${_asciichat_otlp_c_source}"
        BINARY_DIR "${_asciichat_gotel_cache_dir}/otlp_c-build")
else()
    asciichat_fetchcontent_declare(otlp_c
        GIT_REPOSITORY https://github.com/riboseinc/otlp-c.git
        GIT_TAG 0798449d66331c3953b8ba9354164cfbf2f3bdaf
        SOURCE_DIR "${_asciichat_otlp_c_source}"
        BINARY_DIR "${_asciichat_gotel_cache_dir}/otlp_c-build")
endif()
set(_asciichat_gotel_nghttp2_source "${ASCIICHAT_DEPS_CACHE_DIR}/gotel_nghttp2-src")
if(EXISTS "${_asciichat_gotel_nghttp2_source}/CMakeLists.txt")
    asciichat_fetchcontent_declare(gotel_nghttp2
        SOURCE_DIR "${_asciichat_gotel_nghttp2_source}"
        BINARY_DIR "${_asciichat_gotel_cache_dir}/gotel_nghttp2-build")
else()
    asciichat_fetchcontent_declare(gotel_nghttp2
        GIT_REPOSITORY https://github.com/nghttp2/nghttp2.git
        GIT_TAG 43b29f991403b5419dd0bea2840581078ead9bf8
        SOURCE_DIR "${_asciichat_gotel_nghttp2_source}"
        BINARY_DIR "${_asciichat_gotel_cache_dir}/gotel_nghttp2-build")
endif()

# Keep Gotel and its fetched otlp-c/Sequel build trees in the persistent
# dependency cache. Generated Gotel sources are emitted into this same stable
# tree so the shared Debug dependency build does not depend on the parent
# application's binary directory.
# Gotel's gRPC adapter requires the pinned nghttp2 API used by grpc.c. Do not
# substitute an older system package; the child project uses the cache-backed
# source checkout when no compatible target is preconfigured.
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
# Child dependencies need compiler/generator-specific FetchContent binary
# trees. Reusing the unqualified dependency root lets Ninja, MSYS Makefiles,
# and Visual Studio overwrite one another's generated build metadata. The
# source checkouts remain explicitly pinned above and are still shared by all
# configurations; only generated child build files are isolated here.
set(_asciichat_parent_fetchcontent_base_dir
    "${_asciichat_gotel_cache_dir}/fetch")
set(FETCHCONTENT_BASE_DIR "${_asciichat_parent_fetchcontent_base_dir}" CACHE PATH
    "Persistent dependency source/build cache" FORCE)
# A prior parent configure may have cached child binary directories under the
# old unqualified base.  Keep the source checkouts, but let FetchContent
# regenerate each child build tree under the selected compiler/generator.
foreach(_asciichat_fetchcontent_name IN ITEMS sequel otlp_c gotel_nghttp2)
    unset(${_asciichat_fetchcontent_name}_BINARY_DIR CACHE)
    unset(${_asciichat_fetchcontent_name}_BINARY_DIR)
endforeach()
set(sequel_BINARY_DIR "${_asciichat_parent_fetchcontent_base_dir}/sequel-build"
    CACHE PATH "Sequel FetchContent build directory" FORCE)
set(otlp_c_BINARY_DIR "${_asciichat_parent_fetchcontent_base_dir}/otlp_c-build"
    CACHE PATH "otlp-c FetchContent build directory" FORCE)
set(gotel_nghttp2_BINARY_DIR "${_asciichat_parent_fetchcontent_base_dir}/gotel_nghttp2-build"
    CACHE PATH "Gotel nghttp2 FetchContent build directory" FORCE)
if(EXISTS "${_asciichat_sequel_source}/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_SEQUEL "${_asciichat_sequel_source}"
        CACHE PATH "Sequel FetchContent source directory" FORCE)
else()
    unset(FETCHCONTENT_SOURCE_DIR_SEQUEL CACHE)
    unset(FETCHCONTENT_SOURCE_DIR_SEQUEL)
endif()
if(EXISTS "${_asciichat_otlp_c_source}/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_OTLP_C "${_asciichat_otlp_c_source}"
        CACHE PATH "otlp-c FetchContent source directory" FORCE)
else()
    unset(FETCHCONTENT_SOURCE_DIR_OTLP_C CACHE)
    unset(FETCHCONTENT_SOURCE_DIR_OTLP_C)
endif()

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
    set(ENABLE_DOC OFF CACHE BOOL "Disable nghttp2 documentation" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "Disable nghttp2 tests" FORCE)
    # This is an HTTP/2-only build. Do not let nghttp2 probe optional QUIC
    # packages whose generated headers are produced later by ExternalProject.
    set(_asciichat_saved_nghttp2_disable_packages)
    foreach(_package Libngtcp2 Libnghttp3 Systemd Jansson Libevent LibXml2 Jemalloc OpenSSL)
        set(_asciichat_saved_${_package}_disable "${CMAKE_DISABLE_FIND_PACKAGE_${_package}}")
        set(CMAKE_DISABLE_FIND_PACKAGE_${_package} TRUE)
    endforeach()
    asciichat_fetchcontent_declare(asciichat_nghttp2
        GIT_REPOSITORY https://github.com/nghttp2/nghttp2.git
        GIT_TAG 43b29f991403b5419dd0bea2840581078ead9bf8)
    FetchContent_MakeAvailable(asciichat_nghttp2)
    foreach(_package Libngtcp2 Libnghttp3 Systemd Jansson Libevent LibXml2 Jemalloc OpenSSL)
        set(CMAKE_DISABLE_FIND_PACKAGE_${_package} "${_asciichat_saved_${_package}_disable}")
    endforeach()
    unset(_asciichat_saved_nghttp2_disable_packages)
    if(TARGET nghttp2_static)
        set_target_properties(nghttp2_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
        set(GOTEL_NGHTTP2_TARGET nghttp2_static CACHE STRING
            "Reuse the application nghttp2 target for gotel gRPC" FORCE)
    endif()
endif()

if(NOT USE_MUSL)
    unset(GOTEL_NGHTTP2_TARGET CACHE)
    unset(GOTEL_NGHTTP2_TARGET)
    # A package-manager target is already configured on Windows/vcpkg. Reuse
    # it when present; otherwise the gRPC subproject populates the pinned
    # source checkout below.
    find_package(nghttp2 CONFIG QUIET)
    set(_asciichat_nghttp2_vcpkg_root
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    if(NOT EXISTS "${_asciichat_nghttp2_vcpkg_root}")
        set(_asciichat_nghttp2_vcpkg_root
            "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    endif()
    if(EXISTS "${_asciichat_nghttp2_vcpkg_root}")
        unset(_asciichat_nghttp2_include_dir CACHE)
        unset(_asciichat_nghttp2_release_library CACHE)
        unset(_asciichat_nghttp2_debug_library CACHE)
        find_path(_asciichat_nghttp2_include_dir NAMES nghttp2/nghttp2.h
            PATHS "${_asciichat_nghttp2_vcpkg_root}/include" NO_DEFAULT_PATH)
        find_library(_asciichat_nghttp2_release_library NAMES nghttp2
            PATHS "${_asciichat_nghttp2_vcpkg_root}/lib" NO_DEFAULT_PATH)
        find_library(_asciichat_nghttp2_debug_library NAMES nghttp2 nghttp2d
            PATHS "${_asciichat_nghttp2_vcpkg_root}/debug/lib" NO_DEFAULT_PATH)
        if(_asciichat_nghttp2_include_dir AND
           _asciichat_nghttp2_release_library AND
           _asciichat_nghttp2_debug_library)
            if(NOT TARGET asciichat-vcpkg-nghttp2)
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
            endif()
            set(GOTEL_NGHTTP2_TARGET asciichat-vcpkg-nghttp2 CACHE STRING
                "Existing nghttp2 target for gotel gRPC" FORCE)
        endif()
    endif()
endif()

set(_asciichat_gotel_parent_c_flags "${CMAKE_C_FLAGS}")
if(UNIX)
    string(APPEND CMAKE_C_FLAGS " -D_POSIX_C_SOURCE=200809L")
endif()
add_subdirectory(${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/gotel/c
                 "${_asciichat_gotel_cache_dir}/build"
                 EXCLUDE_FROM_ALL)
set(CMAKE_C_FLAGS "${_asciichat_gotel_parent_c_flags}")
unset(_asciichat_gotel_parent_c_flags)

# Expose one application-owned interface after Gotel has configured its gRPC
# provider. Consumers must not rediscover nghttp2 or link a host package.
if(TARGET nghttp2_static)
    set(_asciichat_nghttp2_target nghttp2_static)
elseif(TARGET nghttp2)
    set(_asciichat_nghttp2_target nghttp2)
elseif(TARGET nghttp2::nghttp2)
    set(_asciichat_nghttp2_target nghttp2::nghttp2)
elseif(TARGET asciichat-vcpkg-nghttp2)
    set(_asciichat_nghttp2_target asciichat-vcpkg-nghttp2)
endif()
if(NOT _asciichat_nghttp2_target)
    message(FATAL_ERROR "Gotel requires its cache-backed nghttp2 target")
endif()
if(NOT TARGET asciichat-nghttp2)
    add_library(asciichat-nghttp2 INTERFACE)
    target_link_libraries(asciichat-nghttp2 INTERFACE
        "${_asciichat_nghttp2_target}")
endif()

# Keep the produced libraries beside the cached Gotel build, rather than in
# the parent application's build tree. This makes the cache self-contained
# and lets Ninja reuse valid archives across parent reconfigures.
file(MAKE_DIRECTORY "${_asciichat_gotel_cache_dir}/lib")
foreach(_asciichat_gotel_target gotel otlp_c sequel)
    if(TARGET ${_asciichat_gotel_target})
        set_target_properties(${_asciichat_gotel_target} PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${_asciichat_gotel_cache_dir}/lib"
            LIBRARY_OUTPUT_DIRECTORY "${_asciichat_gotel_cache_dir}/lib"
            RUNTIME_OUTPUT_DIRECTORY "${_asciichat_gotel_cache_dir}/bin")
    endif()
endforeach()

# The vendored Gotel shared target calls its TLS selection helper directly,
# while some Apple linkers do not export that helper from the separately built
# otlp-c dylib.  Keep the helper implementation in Gotel as well so the
# application does not depend on dylib export behavior for this internal API.
if(APPLE AND TARGET gotel)
    target_sources(gotel PRIVATE
        "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/gotel/c/src/tls.c")
    target_link_libraries(gotel PRIVATE OpenSSL::SSL OpenSSL::Crypto "-framework CoreFoundation")
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
# application. Configure it after gotel so ascii-chat does not create a second
# target with the same name before gotel is added.
if(TARGET gotel)
    target_include_directories(gotel PRIVATE
        "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/gotel/c/src")
endif()

# gotel's public SQL header includes sequel.h directly.  The sequel target's
# include directory is not exposed by older gotel revisions, so propagate the
# target-owned header directory to project consumers before recast compiles
# sources that include gotel/gotel.h.
if(TARGET sequel)
    get_target_property(_asciichat_sequel_source_dir sequel SOURCE_DIR)
    if(EXISTS "${_asciichat_sequel_source_dir}/include")
        include_directories("${_asciichat_sequel_source_dir}/include")
    endif()
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
file(CONFIGURE OUTPUT "${_asciichat_gotel_cache_dir}/generated/otlp_exporter.c"
    CONTENT "${_otel_exporter}" @ONLY)
get_target_property(_otel_sources otlp_c SOURCES)
list(FILTER _otel_sources EXCLUDE REGEX "(^|/)exporter\\.c$")
set_property(TARGET otlp_c PROPERTY SOURCES "${_otel_sources}")
target_sources(otlp_c PRIVATE "${_asciichat_gotel_cache_dir}/generated/otlp_exporter.c")

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
