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

@_silgen_name("asciichat_ios_main")
private func asciichat_ios_main(_ mode: Int32) -> Int32

@_silgen_name("asciichat_ios_mode_available")
private func asciichat_ios_mode_available(_ mode: Int32) -> Int32

@_silgen_name("asciichat_ios_is_running")
private func asciichat_ios_is_running() -> Int32

@_silgen_name("asciichat_ios_last_result")
private func asciichat_ios_last_result() -> Int32

@_silgen_name("ios_render_bridge_read_frame")
private func ios_render_bridge_read_frame(
    _ pixels: UnsafeMutablePointer<UInt8>,
    _ capacity: Int,
    _ width: UnsafeMutablePointer<Int32>,
    _ height: UnsafeMutablePointer<Int32>,
    _ pitch: UnsafeMutablePointer<Int32>
) -> Int32

@_silgen_name("ios_render_bridge_write")
private func ios_render_bridge_write(_ fd: Int32, _ data: UnsafeRawPointer, _ length: Int) -> Int

@_silgen_name("ios_render_bridge_reset")
private func ios_render_bridge_reset()

@_silgen_name("ios_render_bridge_sequence")
private func ios_render_bridge_sequence() -> UInt64

@_silgen_name("asciichat_ios_camera_authorized")
private func asciichat_ios_camera_authorized() -> Int32

@_silgen_name("asciichat_ios_camera_request_access")
private func asciichat_ios_camera_request_access()

@_silgen_name("asciichat_ios_camera_start")
private func asciichat_ios_camera_start(_ device: UInt16) -> Int32

@_silgen_name("asciichat_ios_camera_read")
private func asciichat_ios_camera_read(
    _ pixels: UnsafeMutablePointer<UInt32>, _ capacity: Int,
    _ width: UnsafeMutablePointer<Int>, _ height: UnsafeMutablePointer<Int>) -> Int32

@_silgen_name("asciichat_ios_camera_stop")
private func asciichat_ios_camera_stop()

@_silgen_name("asciichat_ios_audio_authorized")
private func asciichat_ios_audio_authorized() -> Int32

@_silgen_name("asciichat_ios_audio_request_access")
private func asciichat_ios_audio_request_access()

@_silgen_name("asciichat_ios_audio_device_count")
private func asciichat_ios_audio_device_count() -> Int32

@_silgen_name("asciichat_ios_audio_device_name")
private func asciichat_ios_audio_device_name(_ index: Int32) -> UnsafePointer<CChar>?

@_silgen_name("asciichat_ios_audio_select_device")
private func asciichat_ios_audio_select_device(_ index: Int32) -> Int32

@_silgen_name("platform_enable_keepawake")
private func platform_enable_keepawake() -> Int32

@_silgen_name("platform_disable_keepawake")
private func platform_disable_keepawake()

@_silgen_name("ios_stdin_bridge_push")
private func ios_stdin_bridge_push(_ data: UnsafeRawPointer, _ length: Int) -> Int

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
            bitmapInfo: [.byteOrder32Little, CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedFirst.rawValue)],
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
    @State private var image: CGImage?
    @State private var cameraMessage = "Waiting for C renderer…"
    @State private var audioMessage = "Checking microphone access…"
    @State private var renderPixels = [UInt8](repeating: 0, count: 4096 * 2048 * 4)
    @State private var lastRenderSequence: UInt64 = 0
    @State private var renderedFrames = 0
    @State private var measuredFPS = 0
    @State private var fpsWindowStart = Date()

    private func readFrame() {
        let sequence = ios_render_bridge_sequence()
        if sequence == lastRenderSequence { return }
        let capacity = 4096 * 2048 * 4
        var width: Int32 = 0
        var height: Int32 = 0
        var pitch: Int32 = 0
        let emitted = renderPixels.withUnsafeMutableBufferPointer { buffer in
            ios_render_bridge_read_frame(buffer.baseAddress!, capacity, &width, &height, &pitch)
        }
        guard emitted != 0, width > 0, height > 0, pitch >= width * 4 else { return }
        let data = Data(renderPixels[0 ..< Int(pitch) * Int(height)])
        guard let provider = CGDataProvider(data: data as CFData) else { return }
        image = CGImage(width: Int(width), height: Int(height), bitsPerComponent: 8, bitsPerPixel: 32,
                        bytesPerRow: Int(pitch), space: CGColorSpaceCreateDeviceRGB(),
                        bitmapInfo: [.byteOrder32Little, CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedFirst.rawValue)],
                        provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
        lastRenderSequence = sequence
        renderedFrames += 1
    }

    var body: some View {
        Group {
            if let image {
                Image(decorative: image, scale: 1)
                    .interpolation(.none)
                    .resizable()
                    .aspectRatio(contentMode: .fit)
            } else {
                VStack {
                    Text("Starting shared mirror pipeline…")
                    Text(cameraMessage).font(.caption).foregroundStyle(.secondary)
                    Text(audioMessage).font(.caption).foregroundStyle(.secondary)
                }
            }
            Text("C render: \(measuredFPS) FPS")
                .font(.caption2.monospacedDigit())
                .foregroundStyle(.secondary)
        }
        .padding()
        .onReceive(Timer.publish(every: 1.0 / 60.0, on: .main, in: .common).autoconnect()) { _ in
            readFrame()
            let now = Date()
            let elapsed = now.timeIntervalSince(fpsWindowStart)
            if elapsed >= 1.0 {
                measuredFPS = Int(Double(renderedFrames) / elapsed)
                renderedFrames = 0
                fpsWindowStart = now
            }
        }
        .onAppear {
            if asciichat_ios_camera_authorized() == 0 {
                cameraMessage = "Camera permission requested"
                asciichat_ios_camera_request_access()
            } else {
                cameraMessage = "C mirror pipeline owns camera capture"
            }
            if asciichat_ios_audio_authorized() == 0 {
                audioMessage = "Microphone permission requested"
                asciichat_ios_audio_request_access()
            } else {
                audioMessage = "Microphone access granted"
            }
            let smokeText = "\u{1b}[2J\u{1b}[H\u{1b}[1;36mascii-chat iOS render bridge\u{1b}[0m\n\nC output is reaching SwiftUI through libvterm + FreeType."
            smokeText.withCString { text in
                _ = ios_render_bridge_write(1, text, smokeText.utf8.count)
            }
        }
        .onDisappear {}
    }
}

private enum IOSMode: Int32, CaseIterable, Identifiable {
    case mirror = 0
    case client = 1
    case discovery = 2

    var id: Int32 { rawValue }
    var title: String {
        switch self {
        case .mirror: return "Mirror"
        case .client: return "Client"
        case .discovery: return "Discovery"
        }
    }

    var isImplemented: Bool { asciichat_ios_mode_available(rawValue) != 0 }

    var availabilityLabel: String {
        isImplemented ? "Available" : "Coming soon"
    }
}

struct ContentView: View {
    @Environment(\.scenePhase) private var scenePhase
    @State private var selectedMode: IOSMode = .mirror
    @State private var launchMessage = "Ready"
    @State private var didStart = false
    @State private var sessionStarted = false
    @State private var lifecycleTick = 0
    @State private var audioDevices: [String] = []
    @State private var selectedAudioDevice: Int32 = 0
    @State private var commandLine = ""

    private func refreshAudioDevices() {
        let count = max(0, Int(asciichat_ios_audio_device_count()))
        audioDevices = (0 ..< count).compactMap { index in
            guard let pointer = asciichat_ios_audio_device_name(Int32(index)) else { return nil }
            return String(cString: pointer)
        }
        if selectedAudioDevice >= Int32(audioDevices.count) { selectedAudioDevice = 0 }
    }

    var body: some View {
        VStack {
            Text("ascii-chat")
                .font(.largeTitle.bold())
            Text("Choose a mode to start")
                .foregroundStyle(.secondary)
            HStack(spacing: 8) {
                ForEach(IOSMode.allCases) { mode in
                    Button {
                        selectedMode = mode
                    } label: {
                        Text(mode.title + (mode.isImplemented ? "" : " · Soon"))
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.bordered)
                    .tint(selectedMode == mode ? .accentColor : .secondary)
                    .disabled(!mode.isImplemented)
                }
            }
            .accessibilityLabel("Session mode")
            .accessibilityHint("Choose mirror, client, or discovery mode")
            Text(selectedMode.availabilityLabel)
                .font(.caption)
                .foregroundStyle(selectedMode.isImplemented ? .green : .secondary)
            if !audioDevices.isEmpty {
                Picker("Audio route", selection: $selectedAudioDevice) {
                    ForEach(audioDevices.indices, id: \.self) { index in
                        Text(audioDevices[index]).tag(Int32(index))
                    }
                }
                .pickerStyle(.menu)
                .onChange(of: selectedAudioDevice) { device in
                    _ = asciichat_ios_audio_select_device(device)
                }
            }
            Button("Start \(selectedMode.title.lowercased())") {
                launchMessage = "Starting \(selectedMode.title.lowercased())…"
                didStart = true
                sessionStarted = true
                ios_render_bridge_reset()
                _ = platform_enable_keepawake()
                DispatchQueue.global(qos: .userInitiated).async {
                    let result = asciichat_ios_main(selectedMode.rawValue)
                    DispatchQueue.main.async {
                        launchMessage = result == 0 ? "Session ended" : "Session failed (\(result))"
                        sessionStarted = false
                    }
                }
            }
            .buttonStyle(.borderedProminent)
            .disabled(!selectedMode.isImplemented || sessionStarted)
            .accessibilityHint(selectedMode.isImplemented ? "Starts the selected native mode" : "This mode is not implemented yet")
            Text(launchMessage)
                .font(.caption)
                .foregroundStyle(.secondary)
            if didStart {
                Text(asciichat_ios_is_running() != 0 ? "Native session running" : "Native session stopped (\(asciichat_ios_last_result()))")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                    .id(lifecycleTick)
            }
            if didStart {
                HStack {
                    TextField("Send command or answer", text: $commandLine)
                        .textFieldStyle(.roundedBorder)
                    Button("Send") {
                        let text = commandLine + "\n"
                        text.withCString { pointer in
                            _ = ios_stdin_bridge_push(pointer, text.utf8.count)
                        }
                        commandLine = ""
                    }
                }
                Divider()
                TestFrameView()
                CameraAsciiView()
            }
        }
        .padding()
        .onReceive(Timer.publish(every: 0.25, on: .main, in: .common).autoconnect()) { _ in
            lifecycleTick &+= 1
            refreshAudioDevices()
            if sessionStarted && asciichat_ios_is_running() == 0 {
                sessionStarted = false
                launchMessage = "Session ended"
            }
        }
        .onChange(of: scenePhase) { phase in
            switch phase {
            case .active:
                if sessionStarted { _ = platform_enable_keepawake() }
            case .background, .inactive:
                platform_disable_keepawake()
            @unknown default:
                break
            }
        }
        .onDisappear {
            platform_disable_keepawake()
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
