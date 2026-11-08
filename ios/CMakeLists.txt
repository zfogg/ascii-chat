if(NOT PLATFORM_IOS)
    return()
endif()

enable_language(Swift)

add_library(ascii-chat-ios-camera STATIC camera.m)
target_include_directories(ascii-chat-ios-camera PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(ascii-chat-ios-app
    AsciiChatApp.swift
    test_frame.c
)

target_compile_options(ascii-chat-ios-app PRIVATE
    "$<$<COMPILE_LANGUAGE:Swift>:-parse-as-library>"
)

target_link_libraries(ascii-chat-ios-app PRIVATE ascii-chat-ios-camera)
target_link_options(ascii-chat-ios-app PRIVATE
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
