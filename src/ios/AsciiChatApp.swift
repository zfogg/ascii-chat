import CoreGraphics
import CoreImage
import Foundation
import Observation
import MetalKit
import SwiftUI
import UIKit

private extension Notification.Name {
    static let iosStopSession = Notification.Name("ascii-chat.ios.stop-session")
    static let iosSessionStarted = Notification.Name("ascii-chat.ios.session-started")
    static let iosSessionEnded = Notification.Name("ascii-chat.ios.session-ended")
}

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

@_silgen_name("asciichat_ios_request_stop")
private func asciichat_ios_request_stop()

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

@_silgen_name("ios_render_bridge_configure")
private func ios_render_bridge_configure(_ cols: Int32, _ rows: Int32, _ fontSizePt: Double)

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

@_silgen_name("asciichat_ios_options_snapshot")
private func asciichat_ios_options_snapshot(_ buffer: UnsafeMutablePointer<CChar>, _ capacity: Int) -> Int32

@_silgen_name("asciichat_ios_options_apply_json")
private func asciichat_ios_options_apply_json(_ json: UnsafePointer<CChar>, _ length: Int) -> Int32

@_silgen_name("asciichat_ios_options_set_pending_json")
private func asciichat_ios_options_set_pending_json(_ json: UnsafePointer<CChar>, _ length: Int) -> Int32

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

private final class CRenderView: MTKView {
    private var renderPixels = [UInt8](repeating: 0, count: 4096 * 2048 * 4)
    private var lastSequence: UInt64 = 0
    private var displayLink: CADisplayLink?
    private let commandQueue: MTLCommandQueue?
    private let ciContext: CIContext?

    override init(frame: CGRect, device: MTLDevice?) {
        let metalDevice = device ?? MTLCreateSystemDefaultDevice()
        commandQueue = metalDevice?.makeCommandQueue()
        ciContext = metalDevice.map { CIContext(mtlDevice: $0) }
        super.init(frame: frame, device: metalDevice)
        framebufferOnly = false
        enableSetNeedsDisplay = false
        isPaused = true
    }

    required init(coder: NSCoder) {
        let metalDevice = MTLCreateSystemDefaultDevice()
        commandQueue = metalDevice?.makeCommandQueue()
        ciContext = metalDevice.map { CIContext(mtlDevice: $0) }
        super.init(coder: coder)
        device = metalDevice
        framebufferOnly = false
        enableSetNeedsDisplay = false
        isPaused = true
    }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        displayLink?.invalidate()
        displayLink = nil
        guard window != nil else { return }
        let link = CADisplayLink(target: self, selector: #selector(displayFrame))
        link.preferredFrameRateRange = CAFrameRateRange(minimum: 60, maximum: 60, preferred: 60)
        link.add(to: .main, forMode: .common)
        displayLink = link
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        guard let screen = window?.screen ?? window?.windowScene?.screen else { return }
        let scale = screen.nativeScale
        let pixelWidth = max(1, Int(ceil(bounds.width * scale)))
        let pixelHeight = max(1, Int(ceil(bounds.height * scale)))
        // The bundled terminal font is rendered at 96 DPI. Match the OS
        // display scale so the C bitmap is generated at native pixel density.
        let fontSizePt = 6.0
        // The terminal renderer uses a cell height of approximately twice its
        // glyph advance. Choose rows from the drawable aspect ratio so the C
        // bitmap is scaled uniformly by Metal instead of squeezing glyphs.
        // The bundled font's measured advance is about 0.83 pixels per point
        // at the renderer's 96-DPI FreeType setup. Keep the cell advance large
        // enough that glyphs remain legible on the native-resolution drawable.
        let cols = max(1, Int(ceil(Double(pixelWidth) / (fontSizePt * 0.8333))))
        let rows = max(1, Int(ceil(Double(cols) * Double(pixelHeight) / Double(pixelWidth) * 0.5)))
        ios_render_bridge_configure(Int32(cols), Int32(rows), fontSizePt)
        layer.contentsScale = scale
    }

    @objc private func displayFrame() {
        let sequence = ios_render_bridge_sequence()
        guard sequence != lastSequence else { return }
        let capacity = renderPixels.count
        var width: Int32 = 0
        var height: Int32 = 0
        var pitch: Int32 = 0
        let emitted = renderPixels.withUnsafeMutableBufferPointer { buffer in
            ios_render_bridge_read_frame(buffer.baseAddress!, capacity, &width, &height, &pitch)
        }
        guard emitted != 0, width > 0, height > 0, pitch >= width * 4 else { return }
        NSLog("iOS render frame width=%d height=%d pitch=%d drawable=%lu×%lu",
              width, height, pitch, currentDrawable?.texture.width ?? 0,
              currentDrawable?.texture.height ?? 0)
        let byteCount = Int(pitch) * Int(height)
        let data = Data(renderPixels[0 ..< byteCount])
        guard let provider = CGDataProvider(data: data as CFData),
              let frame = CGImage(width: Int(width), height: Int(height), bitsPerComponent: 8,
                                  bitsPerPixel: 32, bytesPerRow: Int(pitch),
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: [.byteOrder32Little,
                                               CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedFirst.rawValue)],
                                  provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
        else { return }

        guard let drawable = currentDrawable,
              let commandBuffer = commandQueue?.makeCommandBuffer(),
              let ciContext else { return }
        let image = CIImage(cgImage: frame)
        let drawableWidth = CGFloat(drawable.texture.width)
        let drawableHeight = CGFloat(drawable.texture.height)
        // Map the complete C canvas onto the complete display. Terminal cell
        // proportions are already accounted for when the grid is generated;
        // preserving the bitmap's raw aspect ratio here creates unused bars.
        let fitted = image.transformed(by: CGAffineTransform(
            a: drawableWidth / image.extent.width,
            b: 0,
            c: 0,
            d: drawableHeight / image.extent.height,
            tx: -image.extent.minX * drawableWidth / image.extent.width,
            ty: -image.extent.minY * drawableHeight / image.extent.height))
        ciContext.render(fitted, to: drawable.texture, commandBuffer: commandBuffer,
                         bounds: CGRect(x: 0, y: 0, width: drawableWidth, height: drawableHeight),
                         colorSpace: CGColorSpaceCreateDeviceRGB())
        commandBuffer.present(drawable)
        commandBuffer.commit()
        lastSequence = sequence
    }
}

private struct CRenderRepresentable: UIViewRepresentable {
    func makeUIView(context: Context) -> CRenderView {
        let view = CRenderView(frame: .zero, device: MTLCreateSystemDefaultDevice())
        view.backgroundColor = .black
        return view
    }

    func updateUIView(_ view: CRenderView, context: Context) {}
}

private struct CameraAsciiView: View {
    var body: some View {
        CRenderRepresentable()
            .background(Color.black)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .ignoresSafeArea()
        .onAppear { requestMediaPermissions() }
    }

    private func requestMediaPermissions() {
#if !targetEnvironment(simulator)
        if asciichat_ios_camera_authorized() == 0 { asciichat_ios_camera_request_access() }
#endif
        if asciichat_ios_audio_authorized() == 0 { asciichat_ios_audio_request_access() }
    }
}

extension IOSMode {
    var isImplemented: Bool { asciichat_ios_mode_available(rawValue) != 0 }
}

private enum IOSTheme {
    static let background = Color(red: 0.035, green: 0.047, blue: 0.10)
    static let panel = Color(red: 0.075, green: 0.08, blue: 0.16)
    static let cyan = Color(red: 0.13, green: 0.83, blue: 0.93)
    static let websiteCyan = Color(red: 0.133, green: 0.827, blue: 0.933) // Tailwind cyan-400 (#22d3ee)
    static let websiteGray = Color(red: 0.420, green: 0.447, blue: 0.502) // Tailwind gray-500 (#6b7280)
    static let websiteTeal = Color(red: 0.176, green: 0.831, blue: 0.749) // Tailwind teal-400 (#2dd4bf)
    static let purple = Color(red: 0.77, green: 0.45, blue: 0.98)
    static let teal = Color(red: 0.30, green: 0.90, blue: 0.78)
    static let pink = Color(red: 0.96, green: 0.45, blue: 0.82)
    static let muted = Color(red: 0.68, green: 0.71, blue: 0.80)
}

private struct IOSSettingsView: View {
    let onClose: () -> Void
    let applyJSON: ([String: Any]) -> Void
    @State private var palette = "standard"
    @State private var colorFilter = "none"
    @State private var renderMode = "foreground"
    @State private var colorMode = "truecolor"
    @State private var fps = 60.0
    @State private var audioEnabled = true
    @State private var stretch = true
    @State private var stabilize = false

    private let palettes = ["standard", "blocks", "digital", "minimal", "cool", "custom"]
    private let filters = ["none", "black", "white", "green", "magenta", "fuchsia", "orange", "teal", "cyan", "pink", "red", "yellow", "rainbow"]
    private let renderModes = ["foreground", "background", "half-block"]
    private let colorModes = ["none", "16", "256", "truecolor"]

    var body: some View {
        NavigationStack {
            Form {
                Section("Display") {
                    Picker("Palette", selection: $palette) { ForEach(palettes, id: \.self, content: Text.init) }
                    Picker("Color filter", selection: $colorFilter) { ForEach(filters, id: \.self, content: Text.init) }
                    Picker("Render mode", selection: $renderMode) { ForEach(renderModes, id: \.self, content: Text.init) }
                    Picker("Color mode", selection: $colorMode) { ForEach(colorModes, id: \.self, content: Text.init) }
                    Toggle("Stretch to fill", isOn: $stretch)
                    Toggle("Stabilize frames", isOn: $stabilize)
                    VStack(alignment: .leading) {
                        Text("Frame rate: \(Int(fps)) FPS")
                        Slider(value: $fps, in: 1...144, step: 1)
                    }
                }
                Section("Audio") {
                    Toggle("Audio", isOn: $audioEnabled)
                }
                Section {
                    Button("Apply settings") {
                        applyJSON(["palette": palette, "color_filter": colorFilter, "render_mode": renderMode,
                                   "color_mode": colorMode, "fps": Int(fps), "stretch": stretch,
                                   "stabilize": stabilize, "audio_enabled": audioEnabled])
                    }
                    .foregroundStyle(IOSTheme.websiteCyan)
                }
            }
            .scrollContentBackground(.hidden)
            .background(IOSTheme.background)
            .navigationTitle("Settings")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) { Button("Done", action: onClose).tint(IOSTheme.websiteTeal) }
            }
        }
        .preferredColorScheme(.dark)
    }
}

private struct IOSModeButtonStyle: ButtonStyle {
    let selected: Bool
    let enabled: Bool

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .foregroundStyle(enabled ? (selected ? Color.white : IOSTheme.muted) : IOSTheme.muted.opacity(0.38))
            .background {
                RoundedRectangle(cornerRadius: 12)
                    .fill(selected ? AnyShapeStyle(LinearGradient(colors: [IOSTheme.cyan, IOSTheme.purple], startPoint: .leading, endPoint: .trailing)) : AnyShapeStyle(IOSTheme.panel))
            }
            .overlay { RoundedRectangle(cornerRadius: 12).stroke(selected ? IOSTheme.cyan.opacity(0.8) : Color.white.opacity(0.08), lineWidth: 1) }
            .opacity(configuration.isPressed ? 0.72 : 1)
    }
}

struct ContentView: View {
    @State private var sessionModel = IOSSessionModel()
    @Environment(\.scenePhase) private var scenePhase
    @State private var selectedMode: IOSMode = .mirror
    @State private var launchMessage = ""
    @State private var didStart = false
    @State private var sessionStarted = false
    @State private var sessionStopping = false
    @State private var lifecycleTick = 0
    @State private var audioDevices: [String] = []
    @State private var selectedAudioDevice: Int32 = 0
    @State private var commandLine = ""
    @State private var showSettings = false

    private func applySettings(_ values: [String: Any]) {
        guard JSONSerialization.isValidJSONObject(values),
              let data = try? JSONSerialization.data(withJSONObject: values),
              let json = String(data: data, encoding: .utf8) else { return }
        UserDefaults.standard.set(data, forKey: "ascii-chat.ios.options.patch")
        json.withCString { pointer in
            _ = asciichat_ios_options_set_pending_json(pointer, json.utf8.count)
            _ = asciichat_ios_options_apply_json(pointer, json.utf8.count)
        }
    }

    private func startSelectedSession() {
        let screen = UIScreen.main
        let scale = screen.nativeScale
        let width = max(1, Int(ceil(screen.bounds.width * scale)))
        let height = max(1, Int(ceil(screen.bounds.height * scale)))
        let fontSizePt = 6.0
        let cols = max(1, Int(ceil(Double(width) / (fontSizePt * 0.8333))))
        let rows = max(1, Int(ceil(Double(cols) * Double(height) / Double(width) * 0.5)))
            ios_render_bridge_configure(Int32(cols), Int32(rows), fontSizePt)
        sessionModel.start(mode: selectedMode)
        NotificationCenter.default.post(name: .iosSessionStarted, object: nil)
        launchMessage = "Starting \(selectedMode.title.lowercased())…"
        didStart = true
        sessionStarted = true
        sessionStopping = false
        ios_render_bridge_reset()
        _ = platform_enable_keepawake()
        DispatchQueue.global(qos: .userInitiated).async {
            let result = asciichat_ios_main(selectedMode.rawValue)
            DispatchQueue.main.async {
                sessionModel.finish(result: Int32(result))
                NotificationCenter.default.post(name: .iosSessionEnded, object: nil)
                launchMessage = result == 0 ? "Session ended" : "Session failed (\(result))"
                sessionStarted = false
            }
        }
    }

    private func stopSelectedSession() {
        guard sessionStarted, !sessionStopping else { return }
        sessionStopping = true
        sessionModel.stopRequested()
        launchMessage = "Stopping (selectedMode.title.lowercased())…"
        asciichat_ios_request_stop()
        platform_disable_keepawake()
    }

    private var sessionView: some View {
        GeometryReader { proxy in
            CameraAsciiView()
            .frame(width: proxy.size.width, height: proxy.size.height)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color.black)
        .ignoresSafeArea()
    }

    private var settingsView: some View {
        VStack(spacing: 0) {
            HStack(spacing: 0) {
                Text("ascii").foregroundStyle(IOSTheme.websiteCyan)
                Text("-").foregroundStyle(IOSTheme.websiteGray)
                Text("chat").foregroundStyle(IOSTheme.websiteTeal)
            }
            .font(.system(size: 48, weight: .black, design: .rounded))
            .shadow(color: IOSTheme.websiteCyan.opacity(0.22), radius: 12)
            Text("Video chat in your terminal")
                .font(.system(size: 18, weight: .medium, design: .rounded))
                .foregroundStyle(IOSTheme.muted)
                .padding(.top, 8)
            Text("Choose a mode to start")
                .font(.headline)
                .foregroundStyle(Color.white)
                .padding(.top, 30)
                .padding(.bottom, 14)
            HStack(spacing: 6) {
                ForEach(IOSMode.allCases) { mode in
                    Button { selectedMode = mode } label: {
                        Text(mode.title)
                            .font(.system(size: 14, weight: .semibold, design: .rounded))
                            .lineLimit(1)
                            .minimumScaleFactor(0.8)
                            .frame(maxWidth: .infinity, minHeight: 44)
                    }
                    .buttonStyle(IOSModeButtonStyle(selected: selectedMode == mode, enabled: mode.isImplemented))
                    .disabled(!mode.isImplemented)
                }
            }
            .accessibilityLabel("Session mode")
            .accessibilityHint("Choose mirror, client, or discovery mode")
            if !audioDevices.isEmpty {
                VStack(alignment: .leading, spacing: 8) {
                    Text("Microphone")
                        .font(.subheadline.weight(.semibold))
                        .foregroundStyle(IOSTheme.muted)
                    Picker("Audio route", selection: $selectedAudioDevice) {
                        ForEach(audioDevices.indices, id: \.self) { index in
                            Text(audioDevices[index].hasPrefix("Microphone") ? String(audioDevices[index].dropFirst("Microphone".count)) : audioDevices[index])
                                .tag(Int32(index))
                        }
                    }
                    .labelsHidden()
                    .pickerStyle(.menu)
                    .tint(IOSTheme.cyan)
                    .onChange(of: selectedAudioDevice) { device in _ = asciichat_ios_audio_select_device(device) }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.top, 18)
            }
            Button("Start \(selectedMode.title.lowercased())") { startSelectedSession() }
                .font(.headline)
                .foregroundStyle(Color.white)
                .frame(maxWidth: .infinity, minHeight: 50)
                .background(LinearGradient(colors: [IOSTheme.teal, IOSTheme.cyan, IOSTheme.purple], startPoint: .leading, endPoint: .trailing), in: RoundedRectangle(cornerRadius: 14))
                .disabled(!selectedMode.isImplemented || sessionStarted || sessionStopping)
                .padding(.top, 22)
            if !launchMessage.isEmpty {
                Text(launchMessage)
                    .font(.caption)
                    .foregroundStyle(IOSTheme.muted)
                    .padding(.top, 10)
            }
            if didStart {
                Text(asciichat_ios_is_running() != 0 ? "Native session running" : "Native session stopped (\(asciichat_ios_last_result()))")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                    .id(lifecycleTick)
            }
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 28)
    }

    private func refreshAudioDevices() {
        let count = max(0, Int(asciichat_ios_audio_device_count()))
        audioDevices = (0 ..< count).compactMap { index in
            guard let pointer = asciichat_ios_audio_device_name(Int32(index)) else { return nil }
            return String(cString: pointer)
        }
        if selectedAudioDevice >= Int32(audioDevices.count) { selectedAudioDevice = 0 }
    }

    var body: some View {
        GeometryReader { proxy in
            ZStack {
                IOSTheme.background.ignoresSafeArea()
                    if sessionStarted || sessionStopping {
                        sessionView
                    } else {
                        settingsView
                    }
                    if didStart && !sessionStarted && !sessionStopping {
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
                }
            }
            .frame(width: proxy.size.width, height: proxy.size.height)
        }
        .ignoresSafeArea()
        .onAppear {
            if let data = UserDefaults.standard.data(forKey: "ascii-chat.ios.options.patch"),
               let json = String(data: data, encoding: .utf8) {
                json.withCString { pointer in _ = asciichat_ios_options_set_pending_json(pointer, json.utf8.count) }
            }
            if ProcessInfo.processInfo.arguments.contains("--auto-start-mirror") && !didStart {
                selectedMode = .mirror
                // Let the Metal view complete its first layout pass so the C
                // terminal receives the actual drawable grid dimensions.
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) {
                    guard !didStart else { return }
                    startSelectedSession()
                }
            }
        }
        .task {
            for await status in sessionModel.events() {
                if case .finished = status {
                    lifecycleTick &+= 1
                }
            }
        }
        .onReceive(Timer.publish(every: 0.25, on: .main, in: .common).autoconnect()) { _ in
            lifecycleTick &+= 1
            refreshAudioDevices()
            if sessionStarted && asciichat_ios_is_running() != 0 {
                sessionModel.markRunning()
            }
            if sessionStopping && asciichat_ios_is_running() == 0 {
                sessionStopping = false
                sessionStarted = false
                didStart = false
                launchMessage = ""
                NotificationCenter.default.post(name: .iosSessionEnded, object: nil)
            } else if sessionStarted && asciichat_ios_is_running() == 0 {
                sessionStarted = false
                launchMessage = "Session ended"
                NotificationCenter.default.post(name: .iosSessionEnded, object: nil)
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
        .preferredColorScheme(.dark)
        .onReceive(NotificationCenter.default.publisher(for: .iosStopSession)) { _ in
            stopSelectedSession()
        }
        .overlay(alignment: .topLeading) {
            Button { showSettings = true } label: {
                Image(systemName: "gearshape.fill")
                    .font(.system(size: 20, weight: .semibold))
                    .foregroundStyle(IOSTheme.websiteCyan.opacity(0.9))
                    .frame(width: 56, height: 56)
                    .contentShape(Rectangle())
            }
            .accessibilityLabel("Settings")
            .padding(.top, 8)
            .padding(.leading, 8)
        }
        .sheet(isPresented: $showSettings) {
            IOSSettingsView(onClose: { showSettings = false }, applyJSON: applySettings)
        }
    }
}

@main
final class AsciiChatAppDelegate: UIResponder, UIApplicationDelegate {
    func application(_ application: UIApplication, configurationForConnecting connectingSceneSession: UISceneSession, options: UIScene.ConnectionOptions) -> UISceneConfiguration {
        let configuration = UISceneConfiguration(name: "Default Configuration", sessionRole: connectingSceneSession.role)
        configuration.delegateClass = AsciiChatSceneDelegate.self
        return configuration
    }
}

private final class IOSRootViewController: UIViewController {
    weak var closeButton: UIButton?

    override func viewDidLoad() {
        super.viewDidLoad()
        NotificationCenter.default.addObserver(forName: .iosSessionStarted, object: nil, queue: .main) { [weak self] _ in
            self?.closeButton?.isHidden = false
        }
        NotificationCenter.default.addObserver(forName: .iosSessionEnded, object: nil, queue: .main) { [weak self] _ in
            self?.closeButton?.isHidden = true
        }
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        if let closeButton {
            let statusHeight = view.window?.windowScene?.statusBarManager?.statusBarFrame.height ?? 0
            closeButton.frame = CGRect(x: view.bounds.maxX - 64,
                                        y: statusHeight + 4,
                                        width: 56,
                                        height: 56)
            NSLog("iOS root bounds=(%.1f,%.1f) safeArea=(%.1f,%.1f,%.1f,%.1f) close=(%.1f,%.1f,%.1f,%.1f)",
                  view.bounds.width, view.bounds.height, view.safeAreaInsets.top, view.safeAreaInsets.left,
                  view.safeAreaInsets.bottom, view.safeAreaInsets.right, closeButton.frame.origin.x,
                  closeButton.frame.origin.y, closeButton.frame.width, closeButton.frame.height)
        }
    }
}

final class AsciiChatSceneDelegate: UIResponder, UIWindowSceneDelegate {
    var window: UIWindow?

    func scene(_ scene: UIScene, willConnectTo session: UISceneSession, options connectionOptions: UIScene.ConnectionOptions) {
        guard let windowScene = scene as? UIWindowScene else { return }
        let window = UIWindow(windowScene: windowScene)
        NSLog("iOS scene coordinate=(%.1f,%.1f) screen=(%.1f,%.1f) native=(%.1f,%.1f) scale=%.2f idiom=%ld",
              windowScene.coordinateSpace.bounds.width, windowScene.coordinateSpace.bounds.height,
              windowScene.screen.bounds.width, windowScene.screen.bounds.height,
              windowScene.screen.nativeBounds.width, windowScene.screen.nativeBounds.height,
              windowScene.screen.scale,
              windowScene.traitCollection.userInterfaceIdiom.rawValue)
        window.backgroundColor = UIColor(red: 0.035, green: 0.047, blue: 0.10, alpha: 1)
        let root = IOSRootViewController()
        root.view.backgroundColor = window.backgroundColor
        window.rootViewController = root
        self.window = window
        window.makeKeyAndVisible()

        let hosting = UIHostingController(rootView: ContentView())
        hosting.view.backgroundColor = window.backgroundColor
        root.addChild(hosting)
        hosting.view.translatesAutoresizingMaskIntoConstraints = false
        hosting.edgesForExtendedLayout = [.top, .bottom, .left, .right]
        hosting.additionalSafeAreaInsets = .zero
        root.view.addSubview(hosting.view)
        NSLayoutConstraint.activate([
            hosting.view.leadingAnchor.constraint(equalTo: root.view.leadingAnchor),
            hosting.view.trailingAnchor.constraint(equalTo: root.view.trailingAnchor),
            hosting.view.topAnchor.constraint(equalTo: root.view.topAnchor),
            hosting.view.bottomAnchor.constraint(equalTo: root.view.bottomAnchor),
        ])
        hosting.didMove(toParent: root)

        let closeButton = UIButton(type: .system)
        closeButton.translatesAutoresizingMaskIntoConstraints = false
        closeButton.setImage(UIImage(systemName: "xmark"), for: .normal)
        closeButton.tintColor = UIColor.red.withAlphaComponent(0.5)
        closeButton.isHidden = true
        closeButton.accessibilityLabel = "Stop session"
        closeButton.addAction(UIAction { _ in
            NotificationCenter.default.post(name: .iosStopSession, object: nil)
        }, for: .primaryActionTriggered)
        window.addSubview(closeButton)
        root.closeButton = closeButton
        NSLayoutConstraint.activate([
            closeButton.topAnchor.constraint(equalTo: window.safeAreaLayoutGuide.topAnchor, constant: 4),
            closeButton.trailingAnchor.constraint(equalTo: window.trailingAnchor, constant: -8),
            closeButton.widthAnchor.constraint(equalToConstant: 56),
            closeButton.heightAnchor.constraint(equalToConstant: 56),
        ])
        window.setNeedsLayout()
        NSLog("iOS final window frame=(%.1f,%.1f,%.1f,%.1f) root frame=(%.1f,%.1f,%.1f,%.1f)",
              window.frame.origin.x, window.frame.origin.y, window.frame.width, window.frame.height,
              root.view.frame.origin.x, root.view.frame.origin.y, root.view.frame.width, root.view.frame.height)
    }
}
