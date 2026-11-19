# The isolated ngtcp2 project can discover OpenSSL before it discovers zlib.
message(STATUS "Configuring isolated QUIC dependency prerequisites")
if(USE_MUSL)
    # musl provides pthreads in libc; avoid FindThreads probing glibc-style
    # -lpthread/-lpthreads archives inside this isolated static configure.
    set(CMAKE_HAVE_LIBC_PTHREAD 1 CACHE INTERNAL "musl provides pthreads" FORCE)
    set(CMAKE_USE_PTHREADS_INIT 1 CACHE INTERNAL "musl provides pthreads" FORCE)
    set(CMAKE_THREAD_LIBS_INIT "-pthread" CACHE STRING "musl pthread flags" FORCE)
    set(THREADS_PREFER_PTHREAD_FLAG ON CACHE BOOL "Use compiler pthread flags" FORCE)
endif()
# Recent FindOpenSSL modules expose ZLIB::ZLIB in OpenSSL's interface when
# compression support is enabled, so establish that imported target first.
if(NOT TARGET ZLIB::ZLIB)
    find_package(ZLIB QUIET)
endif()

# Establish the imported OpenSSL targets in the isolated project before its
# crypto target links them.  This hook can run before project(), so use the
# concrete cache paths passed by the parent instead of FindOpenSSL.
if(NOT TARGET OpenSSL::SSL AND OPENSSL_SSL_LIBRARY)
    add_library(OpenSSL::SSL UNKNOWN IMPORTED GLOBAL)
    set_target_properties(OpenSSL::SSL PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_SSL_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "OpenSSL::Crypto")
endif()
if(OPENSSL_INCLUDE_DIR)
    set(OPENSSL_INCLUDE_DIRS "${OPENSSL_INCLUDE_DIR}" CACHE PATH
        "Selected OpenSSL include directory" FORCE)
    set(_OPENSSL_INCLUDE_DIRS "${OPENSSL_INCLUDE_DIR}" CACHE INTERNAL
        "Selected OpenSSL include directory" FORCE)
    set(_OPENSSL_INCLUDEDIR "${OPENSSL_INCLUDE_DIR}" CACHE INTERNAL
        "Selected OpenSSL include directory" FORCE)
endif()
if(NOT TARGET OpenSSL::Crypto AND OPENSSL_CRYPTO_LIBRARY)
    add_library(OpenSSL::Crypto UNKNOWN IMPORTED GLOBAL)
    set_target_properties(OpenSSL::Crypto PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_CRYPTO_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}")
endif()
if(NOT TARGET ZLIB::ZLIB)
    find_path(_quic_zlib_include zlib.h)
    find_library(_quic_zlib_library NAMES z zlib)
    if(_quic_zlib_include AND _quic_zlib_library)
        add_library(ZLIB::ZLIB UNKNOWN IMPORTED GLOBAL)
        set_target_properties(ZLIB::ZLIB PROPERTIES
            IMPORTED_LOCATION "${_quic_zlib_library}"
            INTERFACE_INCLUDE_DIRECTORIES "${_quic_zlib_include}")
    endif()
endif()
