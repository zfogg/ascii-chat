if(NOT PLATFORM_IOS)
    return()
endif()

enable_language(Swift)

unset(IOS_CA_CERTIFICATE_BUNDLE CACHE)
foreach(_ios_ca_candidate
    /usr/local/opt/ca-certificates/share/ca-certificates/cacert.pem
    /opt/homebrew/opt/ca-certificates/share/ca-certificates/cacert.pem
    /usr/local/etc/openssl/cert.pem
    /etc/ssl/cert.pem)
    if(EXISTS "${_ios_ca_candidate}")
        set(IOS_CA_CERTIFICATE_BUNDLE "${_ios_ca_candidate}")
        break()
    endif()
endforeach()
if(NOT IOS_CA_CERTIFICATE_BUNDLE)
    message(FATAL_ERROR "iOS builds require a PEM CA bundle (ca-certificates.pem)")
endif()
set(IOS_CA_CERTIFICATE_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/ca-certificates.pem")
configure_file("${IOS_CA_CERTIFICATE_BUNDLE}" "${IOS_CA_CERTIFICATE_RESOURCE}" COPYONLY)

add_executable(ascii-chat-ios-app
    AsciiChatApp.swift
    IOSKeyboardHandler.swift
    IOSKeyboardInput.swift
    IOSSessionModel.swift
    test_frame.c
    ${APP_SRCS}
    main.c
    "${IOS_CA_CERTIFICATE_RESOURCE}"
    AppIcon.png
    LaunchScreen.storyboard
)
set_source_files_properties("${IOS_CA_CERTIFICATE_RESOURCE}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
set_source_files_properties(AppIcon.png PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
set_source_files_properties(LaunchScreen.storyboard PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

target_include_directories(ascii-chat-ios-app PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common
)
target_compile_definitions(ascii-chat-ios-app PRIVATE
    PLATFORM_IOS=1
    LWS_HAVE_HMAC_CTX_new=1)
if(CMAKE_OSX_SYSROOT MATCHES "Simulator")
    target_compile_definitions(ascii-chat-ios-app PRIVATE ASCIICHAT_IOS_SIMULATOR=1)
endif()

target_compile_options(ascii-chat-ios-app PRIVATE
    "$<$<COMPILE_LANGUAGE:Swift>:-parse-as-library>"
)

get_core_deps_libraries(IOS_CORE_LIBS)

target_link_libraries(ascii-chat-ios-app PRIVATE
    ascii-chat-static-lib
    ascii-chat-static-lib-deps
    ${IOS_CORE_LIBS}
    ${FFMPEG_LIBRARIES}
    ${UTF8PROC_LIBRARIES}
    ${FFTW_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
)
target_link_options(ascii-chat-ios-app PRIVATE
    "LINKER:-lc++"
    "LINKER:-framework,AVFoundation"
    "LINKER:-framework,CoreMedia"
    "LINKER:-framework,CoreVideo"
    "LINKER:-framework,Foundation"
)

set_target_properties(ascii-chat-ios-app PROPERTIES
    Swift_LANGUAGE_VERSION 5
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.ascii-chat.ios"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "17.0"
)

# Keep the simulator runtime check attached to the app target graph without
# making it part of normal builds. The script discovers the booted simulator
# and validates a real non-black mirror frame through the shared C dispatcher.
if(BUILD_IOS_SIM)
    add_custom_target(ascii-chat-ios-simulator-smoke
        DEPENDS ascii-chat-ios-app
        COMMAND "${CMAKE_SOURCE_DIR}/tests/manual/test_ios_simulator_render.sh"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ascii-chat-ios-app.app"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        USES_TERMINAL
        COMMENT "Running iOS simulator mirror smoke test"
    )
    add_custom_target(ascii-chat-ios-settings-smoke
        DEPENDS ascii-chat-ios-app
        COMMAND "${CMAKE_SOURCE_DIR}/tests/manual/test_ios_simulator_settings.sh"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ascii-chat-ios-app.app"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        USES_TERMINAL
        COMMENT "Running iOS simulator settings smoke test"
    )
    add_custom_target(ascii-chat-ios-simulator-client-smoke
        DEPENDS ascii-chat-ios-app
        COMMAND "${CMAKE_SOURCE_DIR}/tests/manual/test_ios_simulator_client.sh"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ascii-chat-ios-app.app"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        USES_TERMINAL
        COMMENT "Running iOS simulator client smoke test against a local server"
    )
endif()

set(_ios_bundle_architecture "${CMAKE_OSX_ARCHITECTURES}")
if(NOT _ios_bundle_architecture)
    if(BUILD_IOS_SIM)
        set(_ios_bundle_architecture "x86_64")
    else()
        set(_ios_bundle_architecture "arm64")
    endif()
endif()

add_custom_target(ascii-chat-ios-bundle-check
    DEPENDS ascii-chat-ios-app
    COMMAND "${CMAKE_SOURCE_DIR}/tests/manual/test_ios_bundle.sh"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ascii-chat-ios-app.app"
            "${_ios_bundle_architecture}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Validating iOS app bundle resources and metadata"
)
