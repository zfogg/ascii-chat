import CoreGraphics
import CoreImage
import Darwin
import Foundation
import Observation
import MetalKit
import SwiftUI
import UIKit

private extension Notification.Name {
    static let iosStopSession = Notification.Name("ascii-chat.ios.stop-session")
    static let iosSessionStarted = Notification.Name("ascii-chat.ios.session-started")
    static let iosSessionEnded = Notification.Name("ascii-chat.ios.session-ended")
    static let iosRenderFontSizeChanged = Notification.Name("ascii-chat.ios.render-font-size-changed")
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

@_silgen_name("options_get")
private func options_get() -> UnsafeRawPointer

@_silgen_name("options_values_json")
private func options_values_json(_ values: UnsafeRawPointer, _ remoteOnly: Bool) -> UnsafeMutablePointer<CChar>?

@_silgen_name("options_apply_json")
private func options_apply_json(_ json: UnsafePointer<CChar>, _ length: Int,
                                _ expectedRevision: UInt64, _ origin: Int32,
                                _ result: UnsafeMutableRawPointer?) -> Int32

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
    private var sourceTexture: MTLTexture?
    private var sourceTextureReady: MTLTexture?
    private var sourceWidth = 0
    private var sourceHeight = 0
    private var sourceBytesPerRow = 0
    private var renderPipeline: MTLRenderPipelineState?
    private var sampler: MTLSamplerState?
    private var displayTicks = 0
    private var displayedFrames = 0
    private var lastFpsLog = CACurrentMediaTime()
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
        configureMetalPipeline(device: metalDevice)
        NotificationCenter.default.addObserver(self, selector: #selector(renderFontSizeChanged),
                                               name: .iosRenderFontSizeChanged, object: nil)
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
        configureMetalPipeline(device: metalDevice)
        NotificationCenter.default.addObserver(self, selector: #selector(renderFontSizeChanged),
                                               name: .iosRenderFontSizeChanged, object: nil)
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
        displayLink?.invalidate()
    }

    @objc private func renderFontSizeChanged() {
        setNeedsLayout()
        layoutIfNeeded()
    }

    private func configureMetalPipeline(device: MTLDevice?) {
        guard let device else { return }
        let shaderSource = """
        #include <metal_stdlib>
        using namespace metal;
        struct VertexOut { float4 position [[position]]; float2 texCoord; };
        vertex VertexOut asciiChatVertex(uint vertexID [[vertex_id]]) {
            constexpr float2 positions[4] = { float2(-1,-1), float2(1,-1), float2(-1,1), float2(1,1) };
            constexpr float2 texCoords[4] = { float2(0,1), float2(1,1), float2(0,0), float2(1,0) };
            return VertexOut{float4(positions[vertexID], 0, 1), texCoords[vertexID]};
        }
        fragment float4 asciiChatFragment(VertexOut in [[stage_in]],
                                          texture2d<float, access::sample> frame [[texture(0)]],
                                          sampler frameSampler [[sampler(0)]]) {
            return frame.sample(frameSampler, in.texCoord);
        }
        """
        let library = try? device.makeLibrary(source: shaderSource, options: nil)
        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = library?.makeFunction(name: "asciiChatVertex")
        descriptor.fragmentFunction = library?.makeFunction(name: "asciiChatFragment")
        descriptor.colorAttachments[0].pixelFormat = colorPixelFormat
        renderPipeline = try? device.makeRenderPipelineState(descriptor: descriptor)
        let samplerDescriptor = MTLSamplerDescriptor()
        samplerDescriptor.minFilter = .nearest
        samplerDescriptor.magFilter = .nearest
        samplerDescriptor.sAddressMode = .clampToEdge
        samplerDescriptor.tAddressMode = .clampToEdge
        sampler = device.makeSamplerState(descriptor: samplerDescriptor)
    }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        displayLink?.invalidate()
        displayLink = nil
        guard window != nil else { return }
        let link = CADisplayLink(target: self, selector: #selector(displayFrame))
        link.preferredFrameRateRange = CAFrameRateRange(minimum: 60, maximum: 120, preferred: 120)
        link.add(to: .main, forMode: .common)
        displayLink = link
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        guard let screen = window?.screen ?? window?.windowScene?.screen else { return }
        let scale = screen.nativeScale
        let pixelWidth = max(1, Int(ceil(bounds.width)))
        let pixelHeight = max(1, Int(ceil(bounds.height)))
        // The bundled terminal font is rendered at 96 DPI. Match the OS
        // display scale so the C bitmap is generated at native pixel density.
        let fontSizePt = UserDefaults.standard.object(forKey: "ascii-chat.ios.render-font-size") as? Double ?? 6.0
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
        displayTicks += 1
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
        guard let drawable = currentDrawable,
              let commandBuffer = commandQueue?.makeCommandBuffer(),
              let metalDevice = device else { return }

        let source = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm,
                                                               width: Int(width), height: Int(height), mipmapped: false)
        source.usage = [.shaderRead]
        if self.sourceTexture == nil || sourceWidth != Int(width) || sourceHeight != Int(height) {
            self.sourceTexture = metalDevice.makeTexture(descriptor: source)
            self.sourceTextureReady = metalDevice.makeTexture(descriptor: source)
            sourceWidth = Int(width)
            sourceHeight = Int(height)
            sourceBytesPerRow = Int(pitch)
        }
        guard let sourceTexture = (displayedFrames % 2 == 0 ? sourceTexture : sourceTextureReady) else { return }
        sourceTexture.replace(region: MTLRegionMake2D(0, 0, Int(width), Int(height)),
                              mipmapLevel: 0, withBytes: renderPixels, bytesPerRow: Int(pitch))

        guard let renderPipeline, let sampler,
              let encoder = commandBuffer.makeRenderCommandEncoder(descriptor: {
                  let descriptor = MTLRenderPassDescriptor()
                  descriptor.colorAttachments[0].texture = drawable.texture
                  descriptor.colorAttachments[0].loadAction = .clear
                  descriptor.colorAttachments[0].storeAction = .store
                  descriptor.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)
                  return descriptor
              }()) else { return }
        encoder.setRenderPipelineState(renderPipeline)
        encoder.setFragmentTexture(sourceTexture, index: 0)
        encoder.setFragmentSamplerState(sampler, index: 0)
        encoder.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
        encoder.endEncoding()
        commandBuffer.present(drawable)
        commandBuffer.commit()
        lastSequence = sequence
        displayedFrames += 1
        let now = CACurrentMediaTime()
        if now - lastFpsLog >= 1.0 {
            NSLog("iOS Metal FPS ticks=%d displayed=%d sequence=%llu", displayTicks, displayedFrames, sequence)
            displayTicks = 0
            displayedFrames = 0
            lastFpsLog = now
        }
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
    static let red = Color(red: 0.95, green: 0.22, blue: 0.28)
    static let muted = Color(red: 0.68, green: 0.71, blue: 0.80)
}

private struct IOSSettingsView: View {
    let onClose: () -> Void
    let applyJSON: ([String: Any]) -> Void
    let renderFontSizeChanged: (Double) -> Void
    @State private var palette = "standard"
    @State private var colorFilter = "none"
    @State private var renderMode = "foreground"
    @State private var colorMode = "truecolor"
    @State private var fps = 0.0
    @State private var audioEnabled = true
    @State private var stretch = true
    @State private var stabilize = false
    @State private var address = "localhost"
    @State private var port = "27224"
    @State private var discoveryServiceURL = ""
    @State private var webcamIndex = "0"
    @State private var microphoneIndex = "0"
    @State private var speakersIndex = "0"
    @State private var microphoneVolume = 1.0
    @State private var renderFontSize = 6.0

    private let palettes = ["standard", "blocks", "digital", "minimal", "cool", "custom"]
    private let filters = ["none", "black", "white", "green", "magenta", "fuchsia", "orange", "teal", "cyan", "pink", "red", "yellow", "rainbow"]
    private let renderModes = ["foreground", "background", "half-block"]
    private let colorModes = ["none", "16", "256", "truecolor"]

    private func loadPersistedValues() {
        guard let data = UserDefaults.standard.data(forKey: "ascii-chat.ios.options.patch"),
              let values = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return }
        if let value = values["render_font_size"] as? NSNumber { renderFontSize = value.doubleValue }
        if let value = values["fps"] as? NSNumber { fps = value.doubleValue }
        if let value = values["palette"] as? String { palette = value }
        if let value = values["color_filter"] as? String { colorFilter = value }
        if let value = values["render_mode"] as? String { renderMode = value }
        if let value = values["color_mode"] as? String { colorMode = value }
        if let value = values["audio_enabled"] as? Bool { audioEnabled = value }
    }

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
                        Text(fps == 0 ? "Frame rate: Auto (camera)" : "Frame rate: \(Int(fps)) FPS")
                        Slider(value: $fps, in: 0...144, step: 1)
                            .onChange(of: fps) { _, value in
                                applyJSON(["fps": Int(value)])
                            }
                    }
                    VStack(alignment: .leading) {
                        Text("Font size: \(renderFontSize, specifier: "%.1f") pt")
                        Slider(value: $renderFontSize, in: 3...18, step: 0.5)
                            .onChange(of: renderFontSize) { _, value in
                                applyJSON(["render_font_size": value])
                                renderFontSizeChanged(value)
                                NotificationCenter.default.post(name: .iosRenderFontSizeChanged, object: nil)
                            }
                    }
                }
                Section("Audio") {
                    Toggle("Audio", isOn: $audioEnabled)
                    LabeledContent("Microphone index") {
                        TextField("Index", text: $microphoneIndex).keyboardType(.numberPad)
                            .multilineTextAlignment(.trailing)
                    }
                    LabeledContent("Speakers index") {
                        TextField("Index", text: $speakersIndex).keyboardType(.numberPad)
                            .multilineTextAlignment(.trailing)
                    }
                    VStack(alignment: .leading) {
                        Text("Microphone volume: \(microphoneVolume, specifier: "%.2f")")
                        Slider(value: $microphoneVolume, in: 0...2, step: 0.05)
                    }
                }
                Section("Camera") {
                    LabeledContent("Webcam index") {
                        TextField("Index", text: $webcamIndex).keyboardType(.numberPad)
                            .multilineTextAlignment(.trailing)
                    }
                }
                Section("Connection") {
                    TextField("Server address", text: $address)
                        .textInputAutocapitalization(.never).autocorrectionDisabled()
                    TextField("Port", text: $port).keyboardType(.numberPad)
                    TextField("Discovery service URL", text: $discoveryServiceURL)
                        .textInputAutocapitalization(.never).autocorrectionDisabled()
                }
                Section("Advanced") {
                    NavigationLink("All ascii-chat options") {
                        IOSAllOptionsView(applyJSON: applyJSON)
                    }
                }
                Section {
                    Button("Apply settings") {
                        applyJSON(["palette": palette, "color_filter": colorFilter, "render_mode": renderMode,
                                   "color_mode": colorMode, "fps": Int(fps), "stretch": stretch,
                                   "stabilize": stabilize, "audio_enabled": audioEnabled,
                                   "address": address, "port": Int(port) ?? 27224,
                                   "discovery_service_url": discoveryServiceURL,
                                   "webcam_index": Int(webcamIndex) ?? 0,
                                   "microphone_index": Int(microphoneIndex) ?? 0,
                                   "speakers_index": Int(speakersIndex) ?? 0,
                                   "microphone_volume": microphoneVolume,
                                   "render_font_size": renderFontSize])
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
            .onAppear { loadPersistedValues() }
        }
        .preferredColorScheme(.dark)
    }
}

private struct IOSAllOptionsView: View {
    let applyJSON: ([String: Any]) -> Void
    @State private var values: [String: Any] = [:]
    @State private var changed = false

    private func loadSnapshot() {
        guard let jsonPointer = options_values_json(options_get(), false) else { return }
        defer { Darwin.free(jsonPointer) }
        let data = Data(bytes: jsonPointer, count: strlen(jsonPointer))
        if let object = try? JSONSerialization.jsonObject(with: data),
           let dictionary = object as? [String: Any] {
            values = dictionary
        }
    }

    var body: some View {
        List {
            ForEach(values.keys.sorted(), id: \.self) { key in
                if let value = values[key] as? Bool {
                    Toggle(key.replacingOccurrences(of: "_", with: " ").capitalized,
                           isOn: Binding(get: { value }, set: { values[key] = $0; changed = true }))
                } else if let value = values[key] as? String,
                          let choices = Self.enumValues(for: key) {
                    Picker(key.replacingOccurrences(of: "_", with: " ").capitalized,
                           selection: Binding(get: { value }, set: { values[key] = $0; changed = true })) {
                        ForEach(choices, id: \.self, content: Text.init)
                    }
                } else if let number = values[key] as? NSNumber,
                          let range = Self.sliderRange(for: key) {
                    VStack(alignment: .leading, spacing: 4) {
                        Text(key.replacingOccurrences(of: "_", with: " ").capitalized)
                            .font(.caption.weight(.semibold))
                            .foregroundStyle(IOSTheme.muted)
                        Text(String(format: "%.1f", number.doubleValue))
                            .font(.caption.monospacedDigit())
                        Slider(value: Binding(
                            get: { (values[key] as? NSNumber)?.doubleValue ?? number.doubleValue },
                            set: { values[key] = $0; changed = true }),
                               in: range.lowerBound...range.upperBound, step: range.step)
                    }
                } else {
                    let currentValue = values[key] ?? ""
                    VStack(alignment: .leading, spacing: 4) {
                        Text(key.replacingOccurrences(of: "_", with: " ").capitalized)
                            .font(.caption.weight(.semibold))
                            .foregroundStyle(IOSTheme.muted)
                        TextField("Value", text: Binding(
                            get: { String(describing: values[key] ?? "") },
                            set: { values[key] = Self.coerce($0, like: currentValue); changed = true }))
                        .textFieldStyle(.roundedBorder)
                    }
                }
            }
            Section {
                Button("Apply all changes") { applyJSON(values); changed = false }
                    .disabled(!changed)
                    .foregroundStyle(IOSTheme.websiteCyan)
            }
        }
        .scrollContentBackground(.hidden)
        .background(IOSTheme.background)
        .navigationTitle("All options")
        .task { loadSnapshot() }
    }

    private static func coerce(_ text: String, like value: Any) -> Any {
        if value is Int { return Int(text) ?? 0 }
        if value is Double { return Double(text) ?? 0 }
        if value is NSNumber { return Double(text) ?? 0 }
        return text
    }

    private static func sliderRange(for key: String) -> (lowerBound: Double, upperBound: Double, step: Double)? {
        switch key {
        case "fps": return (1, 144, 1)
        case "render_font_size": return (3, 18, 0.5)
        case "microphone_volume", "speakers_volume", "volume": return (0, 2, 0.05)
        case "compression_level": return (0, 9, 1)
        case "reconnect_attempts", "webrtc_reconnect_attempts": return (0, 20, 1)
        case "webrtc_ice_timeout": return (0, 60, 1)
        default: return nil
        }
    }

    private static func enumValues(for key: String) -> [String]? {
        switch key {
        case "palette": return ["standard", "blocks", "digital", "minimal", "cool", "custom"]
        case "color_filter": return ["none", "black", "white", "green", "magenta", "fuchsia", "orange", "teal", "cyan", "pink", "red", "yellow", "rainbow"]
        case "render_mode": return ["foreground", "background", "half-block"]
        case "color_mode": return ["none", "16", "256", "truecolor"]
        case "renderer": return ["luminance", "glyph"]
        case "audio_source": return ["microphone", "file", "url", "test"]
        case "audio_capture_source": return ["microphone", "file", "url", "test"]
        case "log_level": return ["DEV", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"]
        case "render_theme": return ["none", "dark", "light"]
        default: return nil
        }
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

private enum TUISwitcherItem: String, CaseIterable, Identifiable {
    case help, logs, stats, sync, errors, update, media

    var id: String { rawValue }

    var title: String {
        switch self {
        case .help: return "Help"
        case .logs: return "Live logs"
        case .stats: return "Statistics"
        case .sync: return "Sync state"
        case .errors: return "Error stacks"
        case .update: return "Check updates"
        case .media: return "Open media"
        }
    }

    var subtitle: String {
        switch self {
        case .help: return "Keyboard shortcuts"
        case .logs: return "Search live output"
        case .stats: return "Runtime metrics"
        case .sync: return "C synchronization"
        case .errors: return "Diagnostic stacks"
        case .update: return "Version checker"
        case .media: return "Path or URL"
        }
    }

    var symbol: String {
        switch self {
        case .help: return "questionmark.circle.fill"
        case .logs: return "text.alignleft"
        case .stats: return "chart.xyaxis.line"
        case .sync: return "arrow.triangle.2.circlepath"
        case .errors: return "exclamationmark.triangle.fill"
        case .update: return "arrow.down.circle.fill"
        case .media: return "play.rectangle.fill"
        }
    }

    var keyBytes: String {
        switch self {
        case .help: return "?"
        case .logs: return "]"
        case .stats: return "="
        case .sync: return "0"
        case .errors: return "`"
        case .update: return "\u{1b}[24~"
        case .media: return "o"
        }
    }
}

private struct TUISwitcher: View {
    let onSelect: (TUISwitcherItem) -> Void
    let onClose: () -> Void

    private let columns = [GridItem(.flexible(), spacing: 12), GridItem(.flexible(), spacing: 12)]

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 20) {
                    VStack(alignment: .leading, spacing: 6) {
                        Text("Choose a TUI")
                            .font(.system(size: 30, weight: .black, design: .rounded))
                            .foregroundStyle(Color.white)
                        Text("Open a native terminal view without leaving your session.")
                            .font(.subheadline)
                            .foregroundStyle(IOSTheme.muted)
                    }
                    LazyVGrid(columns: columns, spacing: 12) {
                        ForEach(TUISwitcherItem.allCases) { item in
                            Button { onSelect(item) } label: {
                                VStack(alignment: .leading, spacing: 10) {
                                    Image(systemName: item.symbol)
                                        .font(.system(size: 22, weight: .semibold))
                                        .foregroundStyle(item == .errors ? IOSTheme.red : IOSTheme.websiteCyan)
                                    Text(item.title)
                                        .font(.headline.weight(.bold))
                                        .foregroundStyle(Color.white)
                                        .lineLimit(1)
                                        .minimumScaleFactor(0.75)
                                    Text(item.subtitle)
                                        .font(.caption)
                                        .foregroundStyle(IOSTheme.muted)
                                        .lineLimit(1)
                                    Spacer(minLength: 0)
                                    Text(item.keyBytes == "\u{1b}[24~" ? "F12" : item.keyBytes)
                                        .font(.system(.caption, design: .monospaced).weight(.semibold))
                                        .foregroundStyle(IOSTheme.websiteTeal)
                                }
                                .frame(maxWidth: .infinity, minHeight: 116, alignment: .leading)
                                .padding(14)
                                .background(IOSTheme.panel, in: RoundedRectangle(cornerRadius: 16))
                                .overlay { RoundedRectangle(cornerRadius: 16).stroke(Color.white.opacity(0.08), lineWidth: 1) }
                            }
                            .buttonStyle(.plain)
                            .accessibilityLabel("Open \(item.title) TUI")
                            .accessibilityHint("Keyboard shortcut \(item.keyBytes == "\u{1b}[24~" ? "F12" : item.keyBytes)")
                        }
                    }
                }
                .padding(20)
            }
            .scrollContentBackground(.hidden)
            .background(IOSTheme.background)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Done", action: onClose).tint(IOSTheme.websiteTeal)
                }
            }
        }
        .preferredColorScheme(.dark)
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
    @State private var showTUISwitcher = false
    @State private var activeTUI: TUISwitcherItem?

    private func applySettings(_ values: [String: Any]) {
        var merged = (UserDefaults.standard.data(forKey: "ascii-chat.ios.options.patch")
            .flatMap { try? JSONSerialization.jsonObject(with: $0) } as? [String: Any]) ?? [:]
        values.forEach { merged[$0.key] = $0.value }
        guard JSONSerialization.isValidJSONObject(merged),
              let data = try? JSONSerialization.data(withJSONObject: merged),
              let json = String(data: data, encoding: .utf8) else { return }
        UserDefaults.standard.set(data, forKey: "ascii-chat.ios.options.patch")
        json.withCString { pointer in
            _ = options_apply_json(pointer, json.utf8.count, UInt64.max, 0, nil)
        }
    }

    private func configureRenderBridge(fontSize: Double) {
        let screen = UIScreen.main
        let scale = screen.nativeScale
        let width = max(1, Int(ceil(screen.bounds.width * scale)))
        let height = max(1, Int(ceil(screen.bounds.height * scale)))
        let cols = max(1, Int(ceil(Double(width) / (fontSize * 0.8333))))
        let rows = max(1, Int(ceil(Double(cols) * Double(height) / Double(width) * 0.5)))
        ios_render_bridge_configure(Int32(cols), Int32(rows), fontSize)
    }

    private func startSelectedSession() {
        let fontSizePt = UserDefaults.standard.object(forKey: "ascii-chat.ios.render-font-size") as? Double ?? 6.0
        configureRenderBridge(fontSize: fontSizePt)
        // Apply persisted options before the native entry point reads GET_OPTION.
        // This keeps the camera's auto-FPS decision stable for the whole session.
        if let data = UserDefaults.standard.data(forKey: "ascii-chat.ios.options.patch"),
           let json = String(data: data, encoding: .utf8) {
            json.withCString { pointer in _ = options_apply_json(pointer, json.utf8.count, UInt64.max, 0, nil) }
        }
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

#if ASCIICHAT_IOS_SIMULATOR
    private func startSimulatorSessionIfNeeded() {
        if !sessionStarted { startSelectedSession() }
    }
#endif

    private func stopSelectedSession() {
        guard sessionStarted, !sessionStopping else { return }
        sessionStopping = true
        sessionModel.stopRequested()
        launchMessage = "Stopping \(selectedMode.title.lowercased())…"
        asciichat_ios_request_stop()
        platform_disable_keepawake()
    }

    private func selectTUI(_ item: TUISwitcherItem) {
        activeTUI = item
        let bytes = Array(item.keyBytes.utf8)
        bytes.withUnsafeBytes { rawBuffer in
            guard let baseAddress = rawBuffer.baseAddress, !bytes.isEmpty else { return }
            _ = ios_stdin_bridge_push(baseAddress, bytes.count)
        }
        // Let the native input loop consume the shortcut and publish its first
        // frame before removing the selector from above the render bridge.
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.12) {
            showTUISwitcher = false
        }
    }

    private func disableTUI() {
        let escape = "\u{1b}"
        escape.withCString { pointer in _ = ios_stdin_bridge_push(pointer, escape.utf8.count) }
        activeTUI = nil
    }

    private var sessionView: some View {
        GeometryReader { proxy in
            ZStack {
                CameraAsciiView()
                    .frame(width: proxy.size.width, height: proxy.size.height)
                if let activeTUI {
                    VStack {
                        HStack {
                            Spacer()
                            Button("Disable TUI") { disableTUI() }
                                .font(.subheadline.weight(.bold))
                                .foregroundStyle(Color.white)
                                .padding(.horizontal, 14)
                                .frame(height: 42)
                                .background(IOSTheme.red, in: Capsule())
                                .accessibilityLabel("Disable \(activeTUI.title) TUI")
                        }
                        .padding(.top, 62)
                        .padding(.horizontal, 14)
                        Spacer()
                    }
                }
            }
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
                json.withCString { pointer in _ = options_apply_json(pointer, json.utf8.count, UInt64.max, 0, nil) }
            }
            if ProcessInfo.processInfo.arguments.contains("--auto-open-settings") {
                showSettings = true
            }
            if ProcessInfo.processInfo.arguments.contains("--auto-start-mirror") && !didStart {
                selectedMode = .mirror
                // Let the Metal view complete its first layout pass so the C
                // terminal receives the actual drawable grid dimensions.
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) {
                    guard !didStart else { return }
                    startSelectedSession()
                    if ProcessInfo.processInfo.arguments.contains("--auto-open-tui") {
                        DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) {
                            showTUISwitcher = true
                        }
                    }
                    if let value = ProcessInfo.processInfo.arguments.first(where: { $0.hasPrefix("--auto-tui=") })?.split(separator: "=", maxSplits: 1).last,
                       let item = TUISwitcherItem(rawValue: String(value)) {
                        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
                            selectTUI(item)
                        }
                    }
                }
            }
            if ProcessInfo.processInfo.arguments.contains("--auto-start-client") && !didStart,
               IOSMode.client.isImplemented {
                selectedMode = .client
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
            HStack(spacing: 4) {
                Button { showSettings = true } label: {
                    Image(systemName: "gearshape.fill")
                        .font(.system(size: 20, weight: .semibold))
                        .foregroundStyle(IOSTheme.websiteCyan.opacity(0.9))
                        .frame(width: 56, height: 56)
                        .contentShape(Rectangle())
                }
                .accessibilityLabel("Settings")
                if sessionStarted {
                    Button { showTUISwitcher = true } label: {
                        Image(systemName: "rectangle.3.group.fill")
                            .font(.system(size: 20, weight: .semibold))
                            .foregroundStyle(IOSTheme.websiteTeal.opacity(0.95))
                            .frame(width: 56, height: 56)
                            .contentShape(Rectangle())
                    }
                    .accessibilityLabel("TUI switcher")
                }
            }
            .padding(.top, 8)
            .padding(.leading, 8)
        }
        .sheet(isPresented: $showSettings) {
            IOSSettingsView(onClose: { showSettings = false }, applyJSON: applySettings) { fontSize in
                UserDefaults.standard.set(fontSize, forKey: "ascii-chat.ios.render-font-size")
                configureRenderBridge(fontSize: fontSize)
            }
        }
        .sheet(isPresented: $showTUISwitcher) {
            TUISwitcher(onSelect: selectTUI, onClose: { showTUISwitcher = false })
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
        let keyboardHandler = IOSKeyboardHandler(frame: .zero)
        keyboardHandler.translatesAutoresizingMaskIntoConstraints = false
        keyboardHandler.isAccessibilityElement = false
        root.view.addSubview(keyboardHandler)
        root.view.addSubview(hosting.view)
        NSLayoutConstraint.activate([
            keyboardHandler.leadingAnchor.constraint(equalTo: root.view.leadingAnchor),
            keyboardHandler.trailingAnchor.constraint(equalTo: root.view.trailingAnchor),
            keyboardHandler.topAnchor.constraint(equalTo: root.view.topAnchor),
            keyboardHandler.bottomAnchor.constraint(equalTo: root.view.bottomAnchor),
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
