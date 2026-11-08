if(NOT PLATFORM_IOS)
    return()
endif()

enable_language(Swift)

add_executable(ascii-chat-ios-app
    AsciiChatApp.swift
    test_frame.c
)

target_compile_options(ascii-chat-ios-app PRIVATE
    "$<$<COMPILE_LANGUAGE:Swift>:-parse-as-library>"
)

target_link_libraries(ascii-chat-ios-app PRIVATE ascii-chat-static-lib-with-deps)
target_link_options(ascii-chat-ios-app PRIVATE
    "LINKER:-framework,AVFoundation"
    "LINKER:-framework,CoreMedia"
    "LINKER:-framework,CoreVideo"
    "LINKER:-framework,Foundation"
)

set_target_properties(ascii-chat-ios-app PROPERTIES
    Swift_LANGUAGE_VERSION 5
    MACOSX_BUNDLE TRUE
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.ascii-chat.ios"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "16.0"
)
