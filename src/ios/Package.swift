// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "AsciiChatIOSModel",
    platforms: [.iOS(.v17)],
    products: [.library(name: "AsciiChatIOSModel", targets: ["AsciiChatIOSModel"])],
    targets: [
        .target(
            name: "AsciiChatIOSModel",
            path: ".",
            sources: ["IOSSessionModel.swift"]
        ),
        .testTarget(
            name: "AsciiChatIOSModelTests",
            dependencies: ["AsciiChatIOSModel"],
            path: "Tests"
        )
    ]
)
