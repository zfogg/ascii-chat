import { useState, useRef, useCallback, useEffect } from "react";

// ── Data ──────────────────────────────────────────────────────────────────────

const COLORS: Record<string, string> = {
  options: "#6366f1",
  media: "#10b981",
  render: "#f59e0b",
  network: "#3b82f6",
  crypto: "#f43f5e",
  discovery: "#8b5cf6",
  session: "#ec4899",
  wasm: "#06b6d4",
  audio: "#84cc16",
  core: "#9090a8",
};

const LEGEND = [
  { key: "options", label: "Options / Config" },
  { key: "media", label: "Media / Video" },
  { key: "render", label: "Render / Encoder" },
  { key: "network", label: "Network / ACIP" },
  { key: "crypto", label: "Crypto / Auth" },
  { key: "discovery", label: "Discovery / ACDS" },
  { key: "session", label: "Session / Lifecycle" },
  { key: "audio", label: "Audio" },
];

interface FileEntry {
  path: string;
  fn: string;
  note?: string;
}
interface FlowNode {
  id: string;
  x: number;
  y: number;
  w: number;
  h: number;
  color: string;
  label: string;
  sublabel?: string;
  description?: string;
  files: FileEntry[];
}
interface FlowEdge {
  from: string;
  to: string;
  label?: string;
  color: string;
}
interface Flow {
  id: string;
  title: string;
  summary: { title: string; text: string };
  nodes: FlowNode[];
  edges: FlowEdge[];
}

const FLOWS: Record<string, Flow> = {
  options: {
    id: "options",
    title: "⚙️ Options & RCU",
    summary: {
      title: "Options Parsing & RCU Publishing",
      text: "How a CLI flag or WASM call becomes a live option. Five stages: mode detect → registry → parse → validate → atomic publish.",
    },
    nodes: [
      {
        id: "argv",
        x: 40,
        y: 220,
        w: 120,
        h: 52,
        color: "core",
        label: "argv / WASM",
        sublabel: "Entry point",
        description:
          "Command-line arguments (argc/argv) on native, or exported C bindings called from JS on WASM.",
        files: [
          {
            path: "src/main.c",
            fn: "main()",
            note: "Receives argc/argv, calls options_init() at line 733",
          },
          {
            path: "lib/platform/wasm/",
            fn: "wasm_set_option()",
            note: "WASM JS→C bridge for setting options programmatically",
          },
        ],
      },
      {
        id: "mode_detect",
        x: 220,
        y: 170,
        w: 140,
        h: 52,
        color: "options",
        label: "Mode Detection",
        sublabel: "+ binary-level flags",
        description:
          "Scans argv for mode keywords (server/client/mirror/discovery-service) or a session string. Parses binary-level flags before mode is known.",
        files: [
          {
            path: "lib/options/options.c",
            fn: "detect_mode_and_parse_binary_options()",
            note: "Determines mode before builder runs",
          },
          {
            path: "lib/options/strings.c",
            fn: "asciichat_mode_from_string()",
            note: 'Maps "server" → MODE_SERVER, etc.',
          },
          {
            path: "lib/discovery/strings.c",
            fn: "is_session_string()",
            note: "Detects session strings → MODE_DISCOVERY",
          },
        ],
      },
      {
        id: "registry",
        x: 220,
        y: 300,
        w: 140,
        h: 52,
        color: "options",
        label: "Registry",
        sublabel: "g_options_registry[]",
        description:
          "A flat array of up to 2048 registry_entry_t descriptors, one per option. Each stores: long_name, short_name, type, byte offset, env_var_name, range [min,max].",
        files: [
          {
            path: "lib/options/registry/core.c",
            fn: "registry_init_from_builders()",
            note: "Merges category arrays into g_options_registry[]",
          },
          {
            path: "lib/options/registry/general.c",
            fn: "g_general_options[]",
            note: "Core options: port, address, fps, width, height",
          },
          {
            path: "lib/options/registry/display.c",
            fn: "g_display_options[]",
            note: "color-mode, palette, render-mode, color-filter",
          },
          {
            path: "lib/options/registry/security.c",
            fn: "g_security_options[]",
            note: "--password, --key, --server-key, --client-keys",
          },
        ],
      },
      {
        id: "preset",
        x: 430,
        y: 190,
        w: 130,
        h: 52,
        color: "options",
        label: "Mode Preset",
        sublabel: "options_preset_unified()",
        description:
          "Returns a preset_config_t for the detected mode. Lists which option groups are active, default overrides, and mutually exclusive flags.",
        files: [
          {
            path: "lib/options/config/presets.c",
            fn: "options_preset_unified()",
            note: "Single preset function covering all modes via mode_bitmask filtering",
          },
          {
            path: "lib/options/registry/mode_defaults.c",
            fn: "apply_mode_defaults()",
            note: "Sets mode-specific defaults",
          },
        ],
      },
      {
        id: "parse",
        x: 430,
        y: 310,
        w: 130,
        h: 52,
        color: "options",
        label: "Arg Parsing",
        sublabel: "builder + parsers",
        description:
          "The option builder walks argv. For each --flag it looks up the registry entry and calls the typed parser. Custom parsers handle timestamps, port ranges, palette strings, PCRE2 grep patterns.",
        files: [
          {
            path: "lib/options/builder/builder.c",
            fn: "options_build()",
            note: "Core argv walking loop; dispatches to typed parsers",
          },
          {
            path: "lib/options/parsing/parsers.c",
            fn: "parse_setting() / parse_enum()",
            note: "PCRE2-validated setting strings",
          },
          {
            path: "lib/options/config/config.c",
            fn: "options_config_parse_toml()",
            note: "Loads and merges TOML config file",
          },
        ],
      },
      {
        id: "env",
        x: 620,
        y: 250,
        w: 130,
        h: 52,
        color: "options",
        label: "Env Vars",
        sublabel: "ASCII_CHAT_*",
        description:
          "After CLI parse, each registry entry's env_var_name is checked. Env vars override config-file values but yield to explicit CLI flags.",
        files: [
          {
            path: "lib/options/parsing/parsers.c",
            fn: "apply_env_var_overrides()",
            note: "Iterates registry, applies env vars with lower priority than CLI",
          },
          {
            path: "lib/options/parsing/validation.c",
            fn: "options_validate()",
            note: "Cross-field validation, range checks, mutually exclusive flags",
          },
        ],
      },
      {
        id: "rcu",
        x: 620,
        y: 360,
        w: 130,
        h: 52,
        color: "options",
        label: "RCU Publish",
        sublabel: "atomic_ptr_store()",
        description:
          "options_state_init() allocates an options_t on the heap and stores a pointer in a global atomic_ptr_t (g_options). Reader threads call GET_OPTION() — a single lock-free atomic_ptr_load with no mutexes.",
        files: [
          {
            path: "lib/options/rcu.c",
            fn: "options_state_init()",
            note: "Allocates initial options_t, publishes to g_options via atomic store",
          },
          {
            path: "lib/options/rcu.c",
            fn: "options_get()",
            note: "Lock-free atomic_ptr_load; falls back to static g_default_options if NULL",
          },
          {
            path: "lib/options/rcu.c",
            fn: "options_update()",
            note: "Copy-on-write: allocate → memcpy → updater → swap → deferred-free old",
          },
          {
            path: "include/ascii-chat/options/rcu.h",
            fn: "GET_OPTION(field)",
            note: "Macro: options_get()->field — used everywhere in the codebase",
          },
        ],
      },
      {
        id: "consumer",
        x: 820,
        y: 300,
        w: 130,
        h: 52,
        color: "session",
        label: "Any Thread",
        sublabel: "GET_OPTION(fps)",
        description:
          "After options_init() returns, any thread — render loop, audio thread, network thread — calls GET_OPTION(field_name) to read the current value. No lock taken.",
        files: [
          {
            path: "lib/video/ascii/ascii.c",
            fn: "ascii_convert()",
            note: "Reads GET_OPTION(color_mode), GET_OPTION(palette_type)",
          },
          {
            path: "lib/audio/audio.c",
            fn: "audio_start()",
            note: "Reads GET_OPTION(audio_enabled), GET_OPTION(speakers_volume)",
          },
          {
            path: "lib/network/acip/server.c",
            fn: "acip_server_loop()",
            note: "Reads GET_OPTION(max_clients), GET_OPTION(port)",
          },
        ],
      },
    ],
    edges: [
      { from: "argv", to: "mode_detect", label: "1", color: "core" },
      { from: "argv", to: "registry", color: "core" },
      { from: "mode_detect", to: "preset", label: "2", color: "options" },
      { from: "registry", to: "preset", color: "options" },
      { from: "registry", to: "parse", color: "options" },
      { from: "preset", to: "parse", label: "3", color: "options" },
      { from: "parse", to: "env", label: "4", color: "options" },
      { from: "env", to: "rcu", label: "5", color: "options" },
      { from: "rcu", to: "consumer", label: "6", color: "session" },
    ],
  },
  media: {
    id: "media",
    title: "🎥 Media Pipeline",
    summary: {
      title: "Input → ASCII → Terminal",
      text: "Webcam / file / URL / stdin → RGBA decode → SIMD ASCII convert → ANSI terminal write.",
    },
    nodes: [
      {
        id: "input_webcam",
        x: 40,
        y: 80,
        w: 120,
        h: 48,
        color: "media",
        label: "Webcam",
        sublabel: "V4L2 / AVFoundation",
        description:
          "Live webcam capture. V4L2 on Linux, AVFoundation on macOS. Frame captured as YUV, converted to RGBA via SwsContext.",
        files: [
          {
            path: "lib/video/webcam/webcam.c",
            fn: "webcam_capture_frame()",
            note: "V4L2 ioctl VIDIOC_DQBUF → YUV → RGBA",
          },
          {
            path: "lib/video/webcam/webcam.c",
            fn: "webcam_init()",
            note: "Opens /dev/videoN, sets format, requests buffers",
          },
        ],
      },
      {
        id: "input_file",
        x: 40,
        y: 165,
        w: 120,
        h: 48,
        color: "media",
        label: "File / URL",
        sublabel: "--file / --url",
        description:
          "Local files or HTTP/HTTPS/RTSP streams opened by FFmpeg. Smart resolver: tries FFmpeg-native extension first, then yt-dlp subprocess for YouTube/Twitch/etc.",
        files: [
          {
            path: "lib/media/source.c",
            fn: "url_has_ffmpeg_native_extension()",
            note: "Checks .mp4/.mkv/.m3u8/rtsp:// for direct FFmpeg open",
          },
          {
            path: "lib/media/yt_dlp.c",
            fn: "yt_dlp_extract_url()",
            note: "Spawns yt-dlp subprocess, caches result 30s in memory",
          },
          {
            path: "lib/media/ffmpeg_decoder.c",
            fn: "ffmpeg_decoder_create()",
            note: "Opens AVFormatContext, finds video/audio streams",
          },
        ],
      },
      {
        id: "input_stdin",
        x: 40,
        y: 250,
        w: 120,
        h: 48,
        color: "media",
        label: "Stdin Pipe",
        sublabel: "--file '-'",
        description:
          "FFmpeg reads from stdin via a custom AVIO context with a 64 KB ring buffer. Enables: ffmpeg -i input.mp4 -f rawvideo - | ascii-chat mirror --file '-'",
        files: [
          {
            path: "lib/media/ffmpeg_decoder.c",
            fn: "avio_alloc_context()",
            note: "64 KB AVIO buffer reading from fd 0 (stdin)",
          },
        ],
      },
      {
        id: "input_test",
        x: 40,
        y: 335,
        w: 120,
        h: 48,
        color: "media",
        label: "Test Pattern",
        sublabel: "--test-pattern",
        description:
          "Synthetic 320×240 RGBA frames generated in memory. Animated color sweep pattern for debugging rendering without a real camera.",
        files: [
          {
            path: "lib/media/source.c",
            fn: "media_source_generate_test_frame()",
            note: "Reuses a pre-allocated image_t, updates animation phase counter",
          },
        ],
      },
      {
        id: "source",
        x: 230,
        y: 200,
        w: 130,
        h: 52,
        color: "media",
        label: "media_source_t",
        sublabel: "Unified abstraction",
        description:
          "A polymorphic struct with type tag (WEBCAM/FILE/STDIN/TEST) and function-pointer dispatch. Hides all input details from the render loop.",
        files: [
          {
            path: "lib/media/source.c",
            fn: "media_source_create()",
            note: "Factory: examines options, creates correct backend",
          },
          {
            path: "lib/media/source.c",
            fn: "media_source_read_video_frame()",
            note: "Returns image_t* regardless of source type",
          },
          {
            path: "lib/media/source.c",
            fn: "media_source_read_audio()",
            note: "Returns PCM float samples for the audio thread",
          },
        ],
      },
      {
        id: "colorfilter",
        x: 420,
        y: 120,
        w: 130,
        h: 52,
        color: "media",
        label: "Color Filter",
        sublabel: "RGBA pixel ops",
        description:
          "Optional per-pixel RGBA transforms before ASCII conversion. Filters: GRAYSCALE, SEPIA, INVERT, HIGH_CONTRAST, COOL, WARM, MATRIX_GREEN, NIGHT_VISION, CRT.",
        files: [
          {
            path: "lib/video/rgba/color_filter.c",
            fn: "color_filter_apply()",
            note: "In-place pixel transform on image_t RGBA buffer",
          },
          {
            path: "lib/video/anim/digital_rain.c",
            fn: "digital_rain_update()",
            note: "Overlays falling katakana column animation (--matrix)",
          },
        ],
      },
      {
        id: "ascii_conv",
        x: 420,
        y: 250,
        w: 130,
        h: 52,
        color: "media",
        label: "ASCII Convert",
        sublabel: "SIMD dispatcher",
        description:
          "ascii_convert() maps pixel blocks to characters via luminance palette. Runtime SIMD dispatch: AVX2 → SSSE3 → SSE2 on x86, NEON/SVE on ARM, scalar fallback.",
        files: [
          {
            path: "lib/video/ascii/ascii.c",
            fn: "ascii_convert()",
            note: "Main entry: luminance mapping + SIMD dispatch",
          },
          {
            path: "lib/video/ascii/avx2/color.c",
            fn: "ascii_convert_avx2_color()",
            note: "Fastest path: 8 pixels/cycle, 256-color SGR output",
          },
          {
            path: "lib/video/ascii/neon/color.c",
            fn: "ascii_convert_neon_color()",
            note: "ARM NEON path (M1/M2 Mac, Apple Silicon)",
          },
          {
            path: "lib/video/ascii/scalar/foreground.c",
            fn: "ascii_convert_scalar_fg()",
            note: "Portable fallback for any CPU",
          },
        ],
      },
      {
        id: "rle",
        x: 420,
        y: 380,
        w: 130,
        h: 48,
        color: "network",
        label: "RLE Compress",
        sublabel: "network path only",
        description:
          "ascii_rle_encode() compresses runs of repeated char+color sequences. Typical 3–8× compression. Applied before ACIP send, decoded on receive.",
        files: [
          {
            path: "lib/video/ascii/rle.c",
            fn: "ascii_rle_encode() / ascii_rle_decode()",
            note: "Run-length encoding for ASCII frame network payloads",
          },
        ],
      },
      {
        id: "terminal",
        x: 620,
        y: 200,
        w: 130,
        h: 52,
        color: "session",
        label: "Terminal Write",
        sublabel: "write() → stdout/fd",
        description:
          "Writes the ASCII+ANSI string to the terminal fd. Uses \\033[H cursor reposition (not clear-screen) to minimize flicker.",
        files: [
          {
            path: "lib/video/terminal/ansi.c",
            fn: "ansi_write_frame()",
            note: "Repositions cursor, writes full ANSI frame atomically",
          },
          {
            path: "lib/platform/terminal.c",
            fn: "terminal_should_use_control_sequences()",
            note: "Returns false when stdout is piped (not a TTY)",
          },
        ],
      },
      {
        id: "audio",
        x: 620,
        y: 340,
        w: 130,
        h: 48,
        color: "audio",
        label: "Audio Thread",
        sublabel: "PortAudio / Opus",
        description:
          "Parallel audio path: PCM samples → Opus encoder → AUDIO_OPUS_BATCH packet over network. On receive: Opus decode → PortAudio playback.",
        files: [
          {
            path: "lib/audio/audio.c",
            fn: "audio_start()",
            note: "Starts PortAudio stream, capture ring buffer",
          },
          {
            path: "lib/audio/audio.c",
            fn: "audio_encode_opus()",
            note: "Encodes PCM → Opus, queues for network send",
          },
        ],
      },
    ],
    edges: [
      { from: "input_webcam", to: "source", label: "RGBA", color: "media" },
      { from: "input_file", to: "source", color: "media" },
      { from: "input_stdin", to: "source", color: "media" },
      { from: "input_test", to: "source", color: "media" },
      { from: "source", to: "colorfilter", label: "1 frame", color: "media" },
      { from: "source", to: "ascii_conv", label: "2 convert", color: "media" },
      { from: "colorfilter", to: "ascii_conv", color: "media" },
      {
        from: "ascii_conv",
        to: "terminal",
        label: "3 render",
        color: "session",
      },
      { from: "ascii_conv", to: "rle", label: "4 net", color: "network" },
      { from: "source", to: "audio", label: "PCM", color: "audio" },
    ],
  },
  renderfile: {
    id: "renderfile",
    title: "🎬 Render-File",
    summary: {
      title: "ASCII → Pixel Video File",
      text: "libvterm emulates terminal, FreeType2 rasterizes glyphs, FFmpeg H.264 encodes to .mp4 or stdout.",
    },
    nodes: [
      {
        id: "rf_ascii",
        x: 40,
        y: 200,
        w: 120,
        h: 52,
        color: "media",
        label: "ASCII Frame",
        sublabel: "ANSI escape string",
        description:
          "The ASCII art string (with ANSI SGR color codes) produced by ascii_convert(). Same string that would be written to a real terminal.",
        files: [
          {
            path: "lib/video/ascii/ascii.c",
            fn: "ascii_convert()",
            note: "Produces ANSI-colored ASCII art string",
          },
        ],
      },
      {
        id: "rf_font",
        x: 230,
        y: 100,
        w: 130,
        h: 52,
        color: "render",
        label: "Font Resolution",
        sublabel: "platform_font_resolve()",
        description:
          "Resolves the font for rendering: fontconfig on Linux, CoreText on macOS. Supports path, family name, or embedded binary font data.",
        files: [
          {
            path: "lib/platform/font.c",
            fn: "platform_font_resolve()",
            note: "fontconfig/CoreText lookup → file path or embedded bytes",
          },
          {
            path: "lib/media/font.c",
            fn: "font_get_matrix_data()",
            note: "Returns pointer to compiled-in matrix font binary",
          },
        ],
      },
      {
        id: "rf_vterm",
        x: 230,
        y: 230,
        w: 130,
        h: 52,
        color: "render",
        label: "libvterm",
        sublabel: "Terminal emulation",
        description:
          "libvterm parses ANSI escape sequences and maintains a VTermScreen cell grid. Each cell stores: codepoint, foreground/background RGB, bold/italic flags.",
        files: [
          {
            path: "lib/media/render/terminal.c",
            fn: "term_renderer_create()",
            note: "Initializes VTerm instance, sets up VTermScreen callbacks",
          },
          {
            path: "lib/media/render/terminal.c",
            fn: "term_renderer_render_to_buffer()",
            note: "Iterates VTermScreen cells → renders each via FreeType2",
          },
        ],
      },
      {
        id: "rf_freetype",
        x: 230,
        y: 360,
        w: 130,
        h: 52,
        color: "render",
        label: "FreeType2",
        sublabel: "Glyph cache + raster",
        description:
          "For each VTermScreen cell, FreeType2 loads the Unicode codepoint, renders a bitmap, and blends it onto the RGBA framebuffer. Results cached in a per-renderer uthash table — cache hit rate >99% after first frame.",
        files: [
          {
            path: "lib/media/render/terminal.c",
            fn: "glyph_cache_get()",
            note: "Hash lookup; on miss: FT_Load_Glyph + FT_Render_Glyph → cache",
          },
          {
            path: "lib/media/render/terminal.c",
            fn: "struct terminal_renderer_s",
            note: "Holds VTerm, FT_Library, FT_Face, RGBA framebuffer, glyph_cache",
          },
        ],
      },
      {
        id: "rf_rgba",
        x: 430,
        y: 230,
        w: 130,
        h: 52,
        color: "render",
        label: "RGBA Framebuffer",
        sublabel: "cols×rows → pixels",
        description:
          "A flat RGBA byte array of (cols × cell_w) × (rows × cell_h) pixels. Allocated once per renderer, reused each frame.",
        files: [
          {
            path: "lib/media/render/terminal.c",
            fn: "term_renderer_create()",
            note: "Allocates framebuffer: width_px * height_px * 4 bytes",
          },
          {
            path: "lib/media/render/renderer.c",
            fn: "render_file_push_frame()",
            note: "Calls term_renderer_render_to_buffer → passes pixels to encoder",
          },
        ],
      },
      {
        id: "rf_sws",
        x: 620,
        y: 150,
        w: 130,
        h: 52,
        color: "render",
        label: "SwsContext",
        sublabel: "RGBA → YUV420p",
        description:
          "FFmpeg's software scaler converts the RGBA pixel buffer to YUV420p (format expected by H.264 encoder). Initialized once, reused each frame.",
        files: [
          {
            path: "lib/media/ffmpeg_encoder.c",
            fn: "ffmpeg_encoder_write_frame()",
            note: "sws_scale() RGBA→YUV420p, then avcodec_send_frame()",
          },
        ],
      },
      {
        id: "rf_encoder",
        x: 620,
        y: 280,
        w: 130,
        h: 52,
        color: "render",
        label: "FFmpeg H.264",
        sublabel: "AVCodecContext",
        description:
          "H.264 encoder (libx264 via FFmpeg). Output path '-' routes to stdout via a custom AVIO context, enabling live piping.",
        files: [
          {
            path: "lib/media/ffmpeg_encoder.c",
            fn: "ffmpeg_encoder_create()",
            note: "Opens output container; stdout path uses avio_alloc_context with write callback",
          },
          {
            path: "lib/media/ffmpeg_encoder.c",
            fn: "ffmpeg_encoder_write_frame()",
            note: "avcodec_send_frame + avcodec_receive_packet + av_interleaved_write_frame",
          },
        ],
      },
      {
        id: "rf_audio",
        x: 620,
        y: 400,
        w: 130,
        h: 48,
        color: "audio",
        label: "AAC Audio",
        sublabel: "ffmpeg_encoder_write_audio()",
        description:
          "PCM float samples from media source or PortAudio ring buffer are encoded as AAC and muxed alongside H.264 into the mp4 container.",
        files: [
          {
            path: "lib/media/render/renderer.c",
            fn: "render_file_push_audio_from_source()",
            note: "Drains audio ring buffer, submits PCM to encoder",
          },
          {
            path: "lib/media/ffmpeg_encoder.c",
            fn: "ffmpeg_encoder_write_audio()",
            note: "Float PCM → AAC → av_interleaved_write_frame",
          },
        ],
      },
      {
        id: "rf_output",
        x: 820,
        y: 280,
        w: 120,
        h: 52,
        color: "core",
        label: "Output",
        sublabel: ".mp4 or stdout",
        description:
          "The final output: an mp4 file on disk, or a raw H.264 stream written to stdout for live piping.",
        files: [],
      },
    ],
    edges: [
      { from: "rf_font", to: "rf_vterm", label: "font", color: "render" },
      { from: "rf_ascii", to: "rf_vterm", label: "1 parse", color: "render" },
      {
        from: "rf_vterm",
        to: "rf_freetype",
        label: "cell grid",
        color: "render",
      },
      { from: "rf_freetype", to: "rf_rgba", label: "2 blend", color: "render" },
      { from: "rf_vterm", to: "rf_rgba", color: "render" },
      { from: "rf_rgba", to: "rf_sws", label: "3 convert", color: "render" },
      { from: "rf_sws", to: "rf_encoder", label: "YUV420p", color: "render" },
      { from: "rf_encoder", to: "rf_output", label: "4 mux", color: "render" },
      { from: "rf_audio", to: "rf_encoder", label: "AAC", color: "audio" },
    ],
  },
  network: {
    id: "network",
    title: "🌐 Network Protocol",
    summary: {
      title: "Client ↔ Server over TCP / WebSocket / WebRTC",
      text: "Packet: magic+type+length+CRC32+client_id. After crypto handshake, all session data is XSalsa20-Poly1305 encrypted.",
    },
    nodes: [
      {
        id: "net_client",
        x: 40,
        y: 200,
        w: 110,
        h: 52,
        color: "session",
        label: "Client",
        sublabel: "acip_client_connect()",
        description:
          "Client resolves address, opens TCP socket (or WebRTC data channel), wraps in acip_transport_t with vtable. Initiates the crypto handshake.",
        files: [
          {
            path: "lib/network/acip/client.c",
            fn: "acip_client_connect()",
            note: "Creates transport, begins handshake sequence",
          },
          {
            path: "lib/network/tcp/tcp.c",
            fn: "tcp_connect()",
            note: "Resolves address, creates and connects socket",
          },
        ],
      },
      {
        id: "net_server",
        x: 820,
        y: 200,
        w: 110,
        h: 52,
        color: "session",
        label: "Server",
        sublabel: "acip_server_accept()",
        description:
          "Server listens on port 27224. Accepts connections, creates per-client acip_transport_t and crypto_handshake_context_t.",
        files: [
          {
            path: "lib/network/acip/server.c",
            fn: "acip_server_accept()",
            note: "accept() → wraps in transport → starts handshake",
          },
          {
            path: "src/server/main.c",
            fn: "server_main()",
            note: "Manages client list, audio mixer, frame broadcast loop",
          },
        ],
      },
      {
        id: "net_hello",
        x: 200,
        y: 100,
        w: 130,
        h: 52,
        color: "crypto",
        label: "CLIENT_HELLO",
        sublabel: "capabilities exchange",
        description:
          "Client sends PACKET_TYPE_CRYPTO_CLIENT_HELLO declaring its crypto capabilities. Server responds with CRYPTO_CAPABILITIES + CRYPTO_PARAMETERS.",
        files: [
          {
            path: "lib/crypto/handshake/client.c",
            fn: "crypto_handshake_client_send_hello()",
            note: "Sends client capabilities packet to server",
          },
          {
            path: "include/ascii-chat/network/packet/packet.h",
            fn: "PACKET_TYPE_CRYPTO_CLIENT_HELLO = 1000",
            note: "First handshake packet type",
          },
        ],
      },
      {
        id: "net_kex",
        x: 430,
        y: 100,
        w: 140,
        h: 52,
        color: "crypto",
        label: "Key Exchange",
        sublabel: "X25519 ephemeral DH",
        description:
          "Server sends KEY_EXCHANGE_INIT: [ephemeral X25519 pubkey][identity Ed25519 pubkey][Ed25519 signature]. Both derive shared secret via crypto_box_beforenm().",
        files: [
          {
            path: "lib/crypto/handshake/server.c",
            fn: "crypto_handshake_server_key_exchange()",
            note: "Sends server keys, validates client key, derives shared secret",
          },
          {
            path: "lib/crypto/handshake/client.c",
            fn: "crypto_handshake_client_key_exchange()",
            note: "Receives server keys, validates Ed25519 sig, sends client key",
          },
          {
            path: "lib/crypto/known_hosts.c",
            fn: "known_hosts_verify()",
            note: "TOFU: checks server key against ~/.ssh/known_hosts",
          },
        ],
      },
      {
        id: "net_auth",
        x: 430,
        y: 240,
        w: 140,
        h: 52,
        color: "crypto",
        label: "Auth (optional)",
        sublabel: "password / SSH key",
        description:
          "If --password: server sends CRYPTO_AUTH_CHALLENGE (random nonce), client hashes password+nonce. If --key/--server-key: Ed25519 identity keys used in key exchange.",
        files: [
          {
            path: "lib/crypto/handshake/server.c",
            fn: "crypto_handshake_server_auth_challenge()",
            note: "Sends random nonce challenge",
          },
          {
            path: "lib/crypto/handshake/client.c",
            fn: "crypto_handshake_client_auth_response()",
            note: "Computes HMAC-SHA256(password, nonce), sends response",
          },
          {
            path: "lib/crypto/ssh/ssh_keys.c",
            fn: "ssh_key_parse_ed25519()",
            note: "Parses PEM/OpenSSH ed25519 key files",
          },
        ],
      },
      {
        id: "net_complete",
        x: 620,
        y: 100,
        w: 130,
        h: 52,
        color: "crypto",
        label: "Handshake Done",
        sublabel: "encrypted session",
        description:
          "Server sends PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE. Both sides now have the shared XSalsa20-Poly1305 key. All subsequent packets use type PACKET_TYPE_ENCRYPTED (1200).",
        files: [
          {
            path: "include/ascii-chat/network/packet/packet.h",
            fn: "PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE = 1108",
            note: "Final handshake packet; session encryption begins",
          },
          {
            path: "include/ascii-chat/network/packet/packet.h",
            fn: "PACKET_TYPE_ENCRYPTED = 1200",
            note: "All session packets use this outer type",
          },
        ],
      },
      {
        id: "net_packet",
        x: 200,
        y: 320,
        w: 130,
        h: 70,
        color: "network",
        label: "Packet Header",
        sublabel: "24 bytes on wire",
        description:
          'Every packet: [magic:8B] hex approx of "ASCIICCHAT" | [type:2B] packet_type_t enum | [length:4B] payload size | [crc32:4B] CRC32 over payload | [client_id:4B] sender ID | [payload:N]',
        files: [
          {
            path: "include/ascii-chat/network/packet/packet.h",
            fn: "packet_header_t",
            note: "Packed struct; magic validates protocol, CRC32 validates integrity",
          },
          {
            path: "lib/network/acip/send.c",
            fn: "send_packet_secure()",
            note: "Builds header, optionally encrypts payload, writes to socket",
          },
        ],
      },
      {
        id: "net_frame",
        x: 430,
        y: 380,
        w: 130,
        h: 52,
        color: "media",
        label: "ASCII Frame",
        sublabel: "PACKET_TYPE_ASCII_FRAME",
        description:
          "Client sends ASCII frames as PACKET_TYPE_ASCII_FRAME (3000), optionally RLE-compressed. Server composites grid, broadcasts merged frame back.",
        files: [
          {
            path: "lib/network/acip/handlers.c",
            fn: "acip_handle_ascii_frame()",
            note: "Server receives frame; triggers grid recomposition",
          },
          {
            path: "lib/network/acip/server.c",
            fn: "acip_server_send_ascii_frame()",
            note: "Broadcasts merged ASCII frame to all connected clients",
          },
        ],
      },
    ],
    edges: [
      { from: "net_client", to: "net_hello", label: "1", color: "crypto" },
      { from: "net_hello", to: "net_kex", label: "2", color: "crypto" },
      { from: "net_kex", to: "net_auth", label: "3", color: "crypto" },
      { from: "net_auth", to: "net_complete", label: "4", color: "crypto" },
      {
        from: "net_complete",
        to: "net_server",
        label: "5 connected",
        color: "session",
      },
      {
        from: "net_client",
        to: "net_packet",
        label: "packet",
        color: "network",
      },
      { from: "net_packet", to: "net_frame", label: "6 data", color: "media" },
      {
        from: "net_frame",
        to: "net_server",
        label: "7 broadcast",
        color: "network",
      },
    ],
  },
  discovery: {
    id: "discovery",
    title: "🔍 Discovery (ACDS)",
    summary: {
      title: "Session Discovery & P2P Signalling",
      text: "Servers register a human-readable session string. Clients look it up. WebRTC ICE candidates flow through the same channel for P2P.",
    },
    nodes: [
      {
        id: "disc_server",
        x: 40,
        y: 80,
        w: 120,
        h: 52,
        color: "session",
        label: "ascii-chat server",
        sublabel: "--discovery",
        description:
          "Server starts with --discovery flag. Connects to the ACDS instance (default port 27225). Registers its session.",
        files: [
          {
            path: "src/server/main.c",
            fn: "server_main()",
            note: "Checks GET_OPTION(discovery), initiates ACDS registration",
          },
        ],
      },
      {
        id: "disc_client",
        x: 40,
        y: 300,
        w: 120,
        h: 52,
        color: "session",
        label: "ascii-chat client",
        sublabel: "--discovery",
        description:
          'Client knows only the session string (e.g. "blue-mountain-tiger"). Connects to ACDS to look up the session.',
        files: [
          {
            path: "src/client/main.c",
            fn: "client_main()",
            note: "If discovery mode: connects to ACDS before connecting to server",
          },
        ],
      },
      {
        id: "disc_acds",
        x: 280,
        y: 190,
        w: 130,
        h: 52,
        color: "discovery",
        label: "ACDS Server",
        sublabel: "SQLite session store",
        description:
          "Lightweight signalling server. Stores sessions in SQLite. Handles: SESSION_CREATE, SESSION_LOOKUP, SESSION_JOIN, WebRTC SDP exchange, session expiry.",
        files: [
          {
            path: "src/discovery-service/main.c",
            fn: "acds_main()",
            note: "Listens on port 27225; accepts ACIP connections",
          },
          {
            path: "lib/discovery/session.c",
            fn: "discovery_session_create()",
            note: "Generates adjective-noun-animal session strings",
          },
        ],
      },
      {
        id: "disc_register",
        x: 500,
        y: 80,
        w: 130,
        h: 52,
        color: "discovery",
        label: "SESSION_CREATE",
        sublabel: "register session",
        description:
          "Server sends ACIP_SESSION_CREATE with: session string (auto-generated if empty), port, crypto capabilities, optionally IP. ACDS stores in SQLite, returns ACIP_SESSION_CREATED.",
        files: [
          {
            path: "lib/network/acip/acds_server.c",
            fn: "acds_server_register_session()",
            note: "Sends SESSION_CREATE, awaits SESSION_CREATED",
          },
          {
            path: "include/ascii-chat/network/packet/packet.h",
            fn: "PACKET_TYPE_ACIP_SESSION_CREATE = 6000",
            note: "Registration packet type",
          },
        ],
      },
      {
        id: "disc_lookup",
        x: 500,
        y: 220,
        w: 130,
        h: 52,
        color: "discovery",
        label: "SESSION_LOOKUP",
        sublabel: "find session",
        description:
          "Client sends ACIP_SESSION_LOOKUP with the session string. ACDS returns ACIP_SESSION_INFO containing server address/port. If --webrtc, also facilitates SDP exchange.",
        files: [
          {
            path: "lib/network/acip/acds_client.c",
            fn: "acds_client_lookup_session()",
            note: "Sends SESSION_LOOKUP, parses SESSION_INFO response",
          },
          {
            path: "include/ascii-chat/network/packet/packet.h",
            fn: "PACKET_TYPE_ACIP_SESSION_LOOKUP = 6002",
            note: "Lookup packet type",
          },
        ],
      },
      {
        id: "disc_webrtc",
        x: 500,
        y: 360,
        w: 130,
        h: 52,
        color: "discovery",
        label: "WebRTC ICE",
        sublabel: "SDP + ICE via ACDS",
        description:
          "Client creates an SDP offer (via libdatachannel), sends ACIP_WEBRTC_SDP to ACDS. ACDS relays to server. Server answers. ICE candidates flow both ways.",
        files: [
          {
            path: "lib/network/webrtc/webrtc.c",
            fn: "webrtc_create_offer()",
            note: "libdatachannel SDP offer creation",
          },
          {
            path: "lib/network/nat/nat.c",
            fn: "nat_discover_port()",
            note: "UPnP port mapping for NAT traversal",
          },
        ],
      },
      {
        id: "disc_mdns",
        x: 280,
        y: 370,
        w: 130,
        h: 52,
        color: "discovery",
        label: "mDNS (LAN)",
        sublabel: "--lan-discovery",
        description:
          "On local networks, the server advertises via mDNS so clients can discover without ACDS. Uses the _asciichat._tcp service type.",
        files: [
          {
            path: "lib/network/mdns/mdns.c",
            fn: "mdns_advertise()",
            note: "Sends mDNS service announcement on local network",
          },
          {
            path: "lib/network/mdns/mdns.c",
            fn: "mdns_browse()",
            note: "Client discovers server via mDNS queries",
          },
        ],
      },
      {
        id: "disc_connect",
        x: 720,
        y: 190,
        w: 120,
        h: 52,
        color: "session",
        label: "Direct Connect",
        sublabel: "TCP / WebRTC",
        description:
          "Once the client has the server address (from ACDS lookup or mDNS), it establishes a direct connection and runs the crypto handshake as normal.",
        files: [
          {
            path: "lib/network/acip/client.c",
            fn: "acip_client_connect()",
            note: "Uses address from discovery to open TCP socket or WebRTC channel",
          },
        ],
      },
    ],
    edges: [
      {
        from: "disc_server",
        to: "disc_acds",
        label: "connect",
        color: "discovery",
      },
      {
        from: "disc_client",
        to: "disc_acds",
        label: "connect",
        color: "discovery",
      },
      {
        from: "disc_acds",
        to: "disc_register",
        label: "1 register",
        color: "discovery",
      },
      {
        from: "disc_register",
        to: "disc_acds",
        label: "store",
        color: "discovery",
      },
      {
        from: "disc_client",
        to: "disc_lookup",
        label: "2 lookup",
        color: "discovery",
      },
      {
        from: "disc_lookup",
        to: "disc_acds",
        label: "query",
        color: "discovery",
      },
      {
        from: "disc_acds",
        to: "disc_webrtc",
        label: "3 ICE relay",
        color: "discovery",
      },
      {
        from: "disc_acds",
        to: "disc_connect",
        label: "4 addr",
        color: "session",
      },
      {
        from: "disc_client",
        to: "disc_mdns",
        label: "LAN",
        color: "discovery",
      },
      {
        from: "disc_mdns",
        to: "disc_connect",
        label: "found",
        color: "discovery",
      },
      {
        from: "disc_connect",
        to: "disc_server",
        label: "5 connected",
        color: "session",
      },
    ],
  },
};

// ── SVG Diagram ───────────────────────────────────────────────────────────────

interface Transform {
  x: number;
  y: number;
  scale: number;
}

function FlowDiagram({
  flow,
  selectedId,
  onSelectNode,
}: {
  flow: Flow;
  selectedId: string | null;
  onSelectNode: (id: string | null) => void;
}) {
  const svgRef = useRef<SVGSVGElement>(null);
  const [transform, setTransform] = useState<Transform>({
    x: 20,
    y: 20,
    scale: 0.92,
  });
  const dragging = useRef(false);
  const dragStart = useRef({ x: 0, y: 0, tx: 0, ty: 0 });

  // Reset view when flow changes
  useEffect(() => {
    setTransform({ x: 20, y: 20, scale: 0.92 });
  }, [flow.id]);

  const nodeMap = Object.fromEntries(flow.nodes.map((n) => [n.id, n]));

  const connected = selectedId
    ? new Set([
        selectedId,
        ...flow.edges
          .filter((e) => e.from === selectedId || e.to === selectedId)
          .flatMap((e) => [e.from, e.to]),
      ])
    : null;

  const onMouseDown = useCallback(
    (e: React.MouseEvent<SVGSVGElement>) => {
      if ((e.target as Element).closest(".node-g")) return;
      dragging.current = true;
      dragStart.current = {
        x: e.clientX,
        y: e.clientY,
        tx: transform.x,
        ty: transform.y,
      };
      e.preventDefault();
    },
    [transform],
  );

  const onMouseMove = useCallback((e: React.MouseEvent<SVGSVGElement>) => {
    if (!dragging.current) return;
    setTransform((t) => ({
      ...t,
      x: dragStart.current.tx + e.clientX - dragStart.current.x,
      y: dragStart.current.ty + e.clientY - dragStart.current.y,
    }));
  }, []);

  const onMouseUp = useCallback(() => {
    dragging.current = false;
  }, []);

  const onWheel = useCallback((e: React.WheelEvent<SVGSVGElement>) => {
    e.preventDefault();
    const factor = e.deltaY < 0 ? 1.1 : 0.91;
    const rect = svgRef.current!.getBoundingClientRect();
    const mx = e.clientX - rect.left;
    const my = e.clientY - rect.top;
    setTransform((t) => ({
      x: mx + (t.x - mx) * factor,
      y: my + (t.y - my) * factor,
      scale: t.scale * factor,
    }));
  }, []);

  return (
    <svg
      ref={svgRef}
      className="w-full h-full cursor-grab active:cursor-grabbing select-none"
      onMouseDown={onMouseDown}
      onMouseMove={onMouseMove}
      onMouseUp={onMouseUp}
      onMouseLeave={onMouseUp}
      onWheel={onWheel}
      onClick={() => onSelectNode(null)}
      style={{ background: "transparent" }}
    >
      <defs>
        <marker
          id="arrow"
          markerWidth="8"
          markerHeight="8"
          refX="6"
          refY="3"
          orient="auto"
        >
          <path d="M0,0 L0,6 L8,3 z" fill="#3a3a52" />
        </marker>
        {Object.entries(COLORS).map(([key, color]) => (
          <marker
            key={key}
            id={`arrow-${key}`}
            markerWidth="8"
            markerHeight="8"
            refX="6"
            refY="3"
            orient="auto"
          >
            <path d="M0,0 L0,6 L8,3 z" fill={color} fillOpacity="0.8" />
          </marker>
        ))}
      </defs>

      <g
        transform={`translate(${transform.x},${transform.y}) scale(${transform.scale})`}
      >
        {/* Edges */}
        {flow.edges.map((edge, i) => {
          const from = nodeMap[edge.from];
          const to = nodeMap[edge.to];
          if (!from || !to) return null;
          const x1 = from.x + from.w,
            y1 = from.y + from.h / 2;
          const x2 = to.x,
            y2 = to.y + to.h / 2;
          const cx1 = x1 + (x2 - x1) * 0.45,
            cy1 = y1;
          const cx2 = x1 + (x2 - x1) * 0.55,
            cy2 = y2;
          const color = COLORS[edge.color] ?? COLORS["core"];
          const isActive =
            selectedId && (edge.from === selectedId || edge.to === selectedId);
          const isDimmed = selectedId && !isActive;
          return (
            <g key={i}>
              <path
                d={`M${x1},${y1} C${cx1},${cy1} ${cx2},${cy2} ${x2},${y2}`}
                fill="none"
                stroke={color}
                strokeWidth={isActive ? 2 : 1.2}
                strokeOpacity={isDimmed ? 0.12 : isActive ? 1 : 0.45}
                markerEnd={`url(#arrow-${edge.color})`}
              />
              {edge.label && (
                <text
                  x={(x1 + x2) / 2}
                  y={(y1 + y2) / 2 - 6}
                  textAnchor="middle"
                  fontSize="9"
                  fill={color}
                  fillOpacity={isDimmed ? 0.15 : isActive ? 1 : 0.6}
                  fontFamily="monospace"
                >
                  {edge.label}
                </text>
              )}
            </g>
          );
        })}

        {/* Nodes */}
        {flow.nodes.map((node) => {
          const color = COLORS[node.color] ?? COLORS["core"];
          const isActive = node.id === selectedId;
          const isDimmed = connected && !connected.has(node.id);
          const isHighlighted =
            connected && connected.has(node.id) && !isActive;
          return (
            <g
              key={node.id}
              className="node-g"
              transform={`translate(${node.x},${node.y})`}
              onClick={(e) => {
                e.stopPropagation();
                onSelectNode(node.id);
              }}
              style={{ cursor: "pointer" }}
            >
              <rect
                width={node.w}
                height={node.h}
                rx={6}
                ry={6}
                fill={color}
                fillOpacity={isActive ? 0.25 : isDimmed ? 0.04 : 0.12}
                stroke={color}
                strokeWidth={isActive ? 2 : 1}
                strokeOpacity={
                  isDimmed ? 0.2 : isActive ? 1 : isHighlighted ? 0.8 : 0.6
                }
              />
              <text
                x={node.w / 2}
                y={node.sublabel ? 18 : node.h / 2 + 4}
                textAnchor="middle"
                fontSize="11"
                fontWeight="600"
                fontFamily="system-ui, sans-serif"
                fill={color}
                fillOpacity={isDimmed ? 0.25 : 1}
              >
                {node.label}
              </text>
              {node.sublabel && (
                <text
                  x={node.w / 2}
                  y={34}
                  textAnchor="middle"
                  fontSize="8.5"
                  fontFamily="monospace"
                  fill={color}
                  fillOpacity={isDimmed ? 0.2 : 0.65}
                >
                  {node.sublabel}
                </text>
              )}
            </g>
          );
        })}
      </g>
    </svg>
  );
}

// ── Main Page ─────────────────────────────────────────────────────────────────

interface Man3Entry {
  name: string;
  file: string;
  sourcePath: string | null;
}

// Hand-written overrides for the handful of paths whose filename doesn't
// follow the standard encoding (full-path or basename).
const MAN3_OVERRIDES: Record<string, string> = {
  "lib/network/tcp/tcp.c": "/man3/ascii-chat-lib_network_tcp_client.c.html",
  "lib/platform/font.c": "/man3/ascii-chat-platform_font.h.html",
  "lib/video/ascii/avx2/color.c": "/man3/ascii-chat-avx2_color.c.html",
  "lib/video/ascii/neon/color.c": "/man3/ascii-chat-neon_color.c.html",
};

export default function Architecture() {
  const [flowId, setFlowId] = useState("options");
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [man3Files, setMan3Files] = useState<Set<string>>(new Set());
  const [man3BySource, setMan3BySource] = useState<Record<string, string>>({});

  useEffect(() => {
    fetch("/man3/pages.json")
      .then((r) => r.json())
      .then((pages: Man3Entry[]) => {
        // Set of all filenames present on disk
        const files = new Set(pages.map((p) => p.file));

        // sourcePath → file (prefer .c.html / .h.html over bare .html)
        const bySource: Record<string, string> = {};
        for (const p of pages) {
          if (p.sourcePath) {
            const ex = bySource[p.sourcePath];
            if (
              !ex ||
              p.file.includes(".c.html") ||
              p.file.includes(".h.html")
            ) {
              bySource[p.sourcePath] = `/man3/${p.file}`;
            }
          }
        }

        setMan3Files(files);
        setMan3BySource(bySource);
      })
      .catch(() => {
        // Fail silently — links won't appear
      });
  }, []);

  // Return a /man3/... URL for a source path, or null if no page exists.
  // Strategy (first match wins):
  //   1. Hand-written override
  //   2. sourcePath index from pages.json
  //   3. Full path encoded:  lib/foo/bar.c → ascii-chat-lib_foo_bar.c.html
  //   4. Strip lib/ prefix:  lib/foo/bar.c → ascii-chat-foo_bar.c.html
  //   5. Basename only:      lib/foo/bar.c → ascii-chat-bar.c.html
  const getMan3Url = (path: string): string | null => {
    const p = path.replace(/\/$/, ""); // strip trailing slash

    if (MAN3_OVERRIDES[p]) return MAN3_OVERRIDES[p];
    if (man3BySource[p]) return man3BySource[p];

    const encode = (s: string) =>
      "ascii-chat-" + s.replace(/\//g, "_") + ".html";

    const candidates = [
      encode(p),
      p.startsWith("lib/") ? encode(p.slice(4)) : null,
      encode(p.split("/").at(-1) ?? p),
    ];

    for (const c of candidates) {
      if (c && man3Files.has(c)) return `/man3/${c}`;
    }
    return null;
  };

  const flow = FLOWS[flowId] ?? FLOWS["options"]!;
  const selectedNode = selectedId
    ? (flow.nodes.find((n) => n.id === selectedId) ?? null)
    : null;

  const handleFlowChange = (id: string) => {
    setFlowId(id);
    setSelectedId(null);
  };

  return (
    <div
      className="flex flex-col flex-1 bg-gray-950 text-gray-100 overflow-hidden"
      style={{ minHeight: 0 }}
    >
      {/* Tabs */}
      <div className="border-b border-gray-800 bg-gray-950/80 backdrop-blur-sm flex-shrink-0">
        <div className="max-w-5xl mx-auto px-4 flex gap-1 py-2 flex-wrap">
          {Object.values(FLOWS).map((f) => (
            <button
              key={f.id}
              onClick={() => handleFlowChange(f.id)}
              className={`px-3 py-1.5 rounded text-sm font-medium transition-colors ${
                f.id === flowId
                  ? "bg-gray-800 text-white"
                  : "text-gray-400 hover:text-gray-200 hover:bg-gray-800/50"
              }`}
            >
              {f.title}
            </button>
          ))}
        </div>
      </div>

      {/* Legend */}
      <div className="border-b border-gray-800/50 flex-shrink-0 px-4 py-2">
        <div className="max-w-5xl mx-auto flex flex-wrap gap-x-4 gap-y-1">
          {LEGEND.map((item) => (
            <div
              key={item.key}
              className="flex items-center gap-1.5 text-xs text-gray-400"
            >
              <div
                className="w-2 h-2 rounded-full flex-shrink-0"
                style={{ background: COLORS[item.key] }}
              />
              {item.label}
            </div>
          ))}
        </div>
      </div>

      {/* Main area: diagram + panel */}
      <div className="flex flex-1 overflow-hidden" style={{ minHeight: 0 }}>
        {/* Left: summary + diagram */}
        <div
          className="flex flex-col flex-1 overflow-hidden"
          style={{ minHeight: 0 }}
        >
          {/* Flow summary */}
          <div className="px-4 py-2 border-b border-gray-800/40 flex-shrink-0">
            <div className="max-w-5xl mx-auto">
              <span className="text-sm font-semibold text-gray-200">
                {flow.summary.title}
              </span>
              <span className="text-sm text-gray-400 ml-2">
                {flow.summary.text}
              </span>
            </div>
          </div>
          {/* SVG diagram */}
          <div className="flex-1 overflow-hidden" style={{ minHeight: 0 }}>
            <FlowDiagram
              flow={flow}
              selectedId={selectedId}
              onSelectNode={setSelectedId}
            />
          </div>
        </div>

        {/* Right panel */}
        <div className="w-72 flex-shrink-0 border-l border-gray-800 flex flex-col overflow-hidden bg-gray-900/40">
          {selectedNode ? (
            <>
              <div className="p-4 border-b border-gray-800 flex-shrink-0">
                <div
                  className="text-base font-semibold"
                  style={{
                    color: COLORS[selectedNode.color] ?? COLORS["core"],
                  }}
                >
                  {selectedNode.label}
                </div>
                {selectedNode.sublabel && (
                  <div className="text-xs text-gray-400 font-mono mt-0.5">
                    {selectedNode.sublabel}
                  </div>
                )}
              </div>
              <div className="flex-1 overflow-y-auto p-4 space-y-4">
                {selectedNode.description && (
                  <p className="text-xs text-gray-300 leading-relaxed">
                    {selectedNode.description}
                  </p>
                )}
                {selectedNode.files.length > 0 && (
                  <div className="space-y-3">
                    {selectedNode.files.map((f, i) => {
                      const color =
                        COLORS[selectedNode.color] ?? COLORS["core"];
                      const man3Url = getMan3Url(f.path);
                      return (
                        <div key={i} className="text-xs">
                          <div className="flex items-start gap-2 mb-1">
                            <div
                              className="w-4 h-4 rounded flex-shrink-0 flex items-center justify-center text-white font-bold"
                              style={{
                                background: color,
                                fontSize: "9px",
                                lineHeight: 1,
                              }}
                            >
                              {i + 1}
                            </div>
                            <div className="min-w-0">
                              {man3Url ? (
                                <a
                                  href={man3Url}
                                  target="_blank"
                                  rel="noopener noreferrer"
                                  className="block font-mono truncate text-gray-200 hover:text-cyan-300 underline underline-offset-2 decoration-gray-600 hover:decoration-cyan-500 transition-colors"
                                  title={f.path}
                                >
                                  {f.path}
                                </a>
                              ) : (
                                <div
                                  className="font-mono truncate text-gray-200"
                                  title={f.path}
                                >
                                  {f.path}
                                </div>
                              )}
                              {man3Url ? (
                                <a
                                  href={man3Url}
                                  target="_blank"
                                  rel="noopener noreferrer"
                                  className="block font-mono mt-0.5 text-purple-300 hover:text-purple-200 underline underline-offset-2 decoration-purple-800 hover:decoration-purple-400 transition-colors"
                                >
                                  {f.fn}
                                </a>
                              ) : (
                                <div className="font-mono mt-0.5 text-purple-300">
                                  {f.fn}
                                </div>
                              )}
                            </div>
                          </div>
                          {f.note && (
                            <div className="text-gray-500 italic ml-6 leading-relaxed">
                              {f.note}
                            </div>
                          )}
                        </div>
                      );
                    })}
                  </div>
                )}
                {selectedNode.files.length === 0 && (
                  <p className="text-xs text-gray-500 italic">
                    No source files for this node.
                  </p>
                )}
              </div>
            </>
          ) : (
            <div className="flex flex-col items-center justify-center flex-1 p-6 text-center gap-3">
              <div className="text-3xl">👆</div>
              <div className="text-sm font-medium text-gray-300">
                Select a node
              </div>
              <div className="text-xs text-gray-500 leading-relaxed">
                Click any node in the diagram to see the relevant source files
                and functions.
              </div>
            </div>
          )}
        </div>
      </div>

      {/* Zoom controls */}
      <div
        className="absolute bottom-4 left-4 flex flex-col gap-1 pointer-events-none"
        style={{ zIndex: 10 }}
      >
        <div className="text-xs text-gray-600 select-none">
          scroll to zoom · drag to pan
        </div>
      </div>
    </div>
  );
}
