import CoreGraphics
import Foundation
import SwiftUI

@_silgen_name("asciichat_ios_test_frame")
private func asciichat_ios_test_frame(
    _ pixels: UnsafeMutablePointer<UInt32>,
    _ capacity: Int,
    _ width: UnsafeMutablePointer<Int>,
    _ height: UnsafeMutablePointer<Int>
) -> Int32

@_silgen_name("asciichat_ios_mirror_main")
private func asciichat_ios_mirror_main() -> Int32

@_silgen_name("asciichat_ios_camera_read_ascii")
private func asciichat_ios_camera_read_ascii(
    _ output: UnsafeMutablePointer<CChar>,
    _ capacity: Int,
    _ columns: Int,
    _ rows: Int,
    _ length: UnsafeMutablePointer<Int>
) -> Int32

@_silgen_name("asciichat_ios_camera_stop")
private func asciichat_ios_camera_stop()

private struct TestFrameView: View {
    private let image: CGImage?

    init() {
        let capacity = 96 * 64
        var pixels = Array(repeating: UInt32(0), count: capacity)
        var width = 0
        var height = 0
        let emitted = pixels.withUnsafeMutableBufferPointer { buffer in
            asciichat_ios_test_frame(buffer.baseAddress!, capacity, &width, &height)
        }

        guard emitted != 0, width > 0, height > 0 else {
            image = nil
            return
        }

        let data = pixels.withUnsafeBytes { Data(bytes: $0.baseAddress!, count: width * height * 4) }
        guard let provider = CGDataProvider(data: data as CFData) else {
            image = nil
            return
        }

        image = CGImage(
            width: width,
            height: height,
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: width * 4,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
            provider: provider,
            decode: nil,
            shouldInterpolate: false,
            intent: .defaultIntent
        )
    }

    var body: some View {
        Group {
            if let image {
                Image(decorative: image, scale: 1)
                    .interpolation(.none)
                    .resizable()
                    .aspectRatio(contentMode: .fit)
                    .padding()
            } else {
                Text("Unable to emit test frame")
            }
        }
    }
}

private struct CameraAsciiView: View {
    @State private var text = "Starting camera…"
    @State private var cameraStarted = false
    private let timer = Timer.publish(every: 1.0 / 15.0, on: .main, in: .common).autoconnect()

    private func readFrame() {
        let columns = 64
        let rows = 32
        var output = Array(repeating: CChar(0), count: rows * (columns + 1) + 1)
        var length = 0
        guard cameraStarted else { return }
        let emitted = output.withUnsafeMutableBufferPointer { buffer in
            asciichat_ios_camera_read_ascii(buffer.baseAddress!, buffer.count, columns, rows, &length)
        }
        guard emitted != 0 else {
            text = "Waiting for camera frame…"
            return
        }
        text = String(decoding: output.prefix(length).map { UInt8(bitPattern: $0) }, as: UTF8.self)
    }

    var body: some View {
        Text(text)
            .font(.system(size: 8, design: .monospaced))
            .lineLimit(nil)
            .padding()
            .onAppear {
                cameraStarted = asciichat_ios_mirror_main() != 0
                if !cameraStarted { text = "Camera unavailable" }
            }
            .onReceive(timer) { _ in readFrame() }
            .onDisappear { asciichat_ios_camera_stop() }
    }
}

struct ContentView: View {
    var body: some View {
        VStack {
            Text("ascii-chat iOS prototype")
            TestFrameView()
            CameraAsciiView()
        }
    }
}

@main
struct AsciiChatApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
        }
    }
}
