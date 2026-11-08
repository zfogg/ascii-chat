if(NOT PLATFORM_IOS)
    return()
endif()

enable_language(Swift)

add_executable(ascii-chat-ios-app
    AsciiChatApp.swift
    test_frame.c
    ${APP_SRCS}
    main.c
)

target_include_directories(ascii-chat-ios-app PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common
)
target_compile_definitions(ascii-chat-ios-app PRIVATE
    PLATFORM_IOS=1
    LWS_HAVE_HMAC_CTX_new=1)

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
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "16.0"
)
