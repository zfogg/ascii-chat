if(NOT PLATFORM_IOS)
    return()
endif()

set(_ios_app_binary "${CMAKE_CURRENT_BINARY_DIR}/ascii-chat-ios-app.bin")
set(_ios_frame_object "${CMAKE_CURRENT_BINARY_DIR}/test_frame.o")
add_custom_command(OUTPUT "${_ios_app_binary}"
    COMMAND /usr/local/Cellar/llvm/23.1.5/bin/clang
        -target "${CMAKE_Swift_COMPILER_TARGET}"
        -isysroot "${CMAKE_OSX_SYSROOT}"
        -c "${CMAKE_CURRENT_SOURCE_DIR}/test_frame.c" -o "${_ios_frame_object}"
    COMMAND /usr/bin/swiftc
        -parse-as-library
        -target "${CMAKE_Swift_COMPILER_TARGET}"
        -sdk "${CMAKE_OSX_SYSROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/AsciiChatApp.swift" "${_ios_frame_object}"
        -o "${_ios_app_binary}"
    DEPENDS AsciiChatApp.swift test_frame.c test_frame.h
    VERBATIM)
add_custom_target(ascii-chat-ios-app ALL DEPENDS "${_ios_app_binary}")
