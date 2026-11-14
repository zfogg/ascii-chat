// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "AsciiChatIOSModel",
    platforms: [.iOS(.v17), .macOS(.v13)],
    products: [.library(name: "AsciiChatIOSModel", targets: ["AsciiChatIOSModel"])],
    targets: [
        .target(
            name: "AsciiChatIOSModel",
            path: ".",
            exclude: [
                "AsciiChatApp.swift",
                "CMakeLists.txt",
                "Info.plist",
                "LaunchScreen.storyboard",
                "AppIcon.png",
                "AppIcon.NOTICE",
                "IOSKeyboardHandler.swift",
                "main.c",
                "main.h",
                "mirror.h",
                "test_frame.c",
                "test_frame.h",
                "Tests"
            ],
            sources: ["IOSSessionModel.swift", "IOSKeyboardInput.swift"]
        ),
        .testTarget(
            name: "AsciiChatIOSModelTests",
            dependencies: ["AsciiChatIOSModel"],
            path: "Tests"
        )
    ]
)
