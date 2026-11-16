# The isolated ngtcp2 project can discover OpenSSL before it discovers zlib.
message(STATUS "Configuring isolated QUIC dependency prerequisites")
# Recent FindOpenSSL modules expose ZLIB::ZLIB in OpenSSL's interface when
# compression support is enabled, so establish that imported target first.
if(NOT TARGET ZLIB::ZLIB)
    find_package(ZLIB QUIET)
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
