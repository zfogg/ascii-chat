import {
  useCallback,
  useEffect,
  useLayoutEffect,
  useMemo,
  useRef,
  useState,
} from "react";

// Extend Window interface for frame metrics
declare global {
  interface Window {
    __clientFrameMetrics?: {
      rendered: number;
      received: number;
      queueDepth: number;
      uniqueRendered?: number;
      frameHashes?: Record<string, number>;
    };
  }
}
import {
  cleanupClientWasm,
  ColorFilter as ClientColorFilter,
  ColorMode as ClientColorMode,
  ConnectionState,
  isWasmReady as isClientWasmReady,
  PacketType,
} from "../wasm/client";
import {
  getColorFilter,
  getColorMode,
  getDimensions,
  getFlipX,
  getMatrixRain,
  getPalette,
  getPaletteChars,
  getTargetFps,
  setColorFilter,
  setColorMode,
  setDimensions,
  setFlipX,
  setMatrixRain,
  setPalette,
  setPaletteChars,
  setTargetFps,
} from "../wasm/settings";
import { ClientConnection } from "../network/ClientConnection";
import { parseAsciiFrame } from "../network/AsciiFrameParser";
import {
  AsciiRenderer,
  AsciiRendererHandle,
} from "../components/AsciiRenderer";
import { ConnectionPanelModal } from "../components/ConnectionPanelModal";
import { Settings, SettingsConfig } from "../components/Settings";
import { WebClientHead } from "../components/WebClientHead";
import { AsciiChatMode } from "../utils/optionsHelp";
import { PageControlBar } from "../components/PageControlBar";
import { PageLayout } from "../components/PageLayout";
import { createWasmOptionsManager } from "../hooks/useWasmOptions";
import { useCanvasCapture } from "../hooks/useCanvasCapture";
import { useRenderLoop } from "../hooks/useRenderLoop";

/**
 * H.265/HEVC encoder using WebCodecs VideoEncoder API.
 * Encodes canvas frames to H.265 bitstream for transmission to server.
 * Requires Chrome 113+ or Edge (Firefox/Safari don't support H.265 encoding).
 */
class H265Encoder {
  private encoder: VideoEncoder | null = null;
  private pendingChunks: Array<{
    flags: number;
    width: number;
    height: number;
    data: Uint8Array;
  }> = [];
  private width = 0;
  private height = 0;
  private frameCount = 0;
  private isOpen = false;

  static isSupported(): boolean {
    return typeof VideoEncoder !== "undefined";
  }

  async initialize(width: number, height: number, fps: number): Promise<void> {
    this.width = width;
    this.height = height;
    this.frameCount = 0;

    // Check H.265 support first
    const bitrate = Math.max(500_000, width * height * 2 * fps);
    try {
      const configSupport = await VideoEncoder.isConfigSupported({
        codec: "hvc1",
        width,
        height,
        bitrate,
        framerate: fps,
      });
      if (!configSupport.supported) {
        console.log(
          "[H265Encoder] H.265 not supported, will use IMAGE_FRAME packets instead",
        );
        this.isOpen = false;
        return; // Skip encoder initialization, will use IMAGE_FRAME fallback
      }
    } catch (err) {
      console.log(
        "[H265Encoder] H.265 check failed, will use IMAGE_FRAME packets instead:",
        err,
      );
      this.isOpen = false;
      return; // Skip encoder initialization, will use IMAGE_FRAME fallback
    }

    this.encoder = new VideoEncoder({
      output: (chunk: EncodedVideoChunk) => {
        const flags = chunk.type === "key" ? 0x01 : 0x00;
        const buffer = new Uint8Array(chunk.byteLength);
        chunk.copyTo(buffer);
        this.pendingChunks.push({
          flags,
          width: this.width,
          height: this.height,
          data: buffer,
        });
      },
      error: (err: DOMException) => {
        console.error("[H265Encoder] Encoding error:", err);
        this.isOpen = false; // Mark encoder as closed on error
      },
    });

    this.encoder.configure({
      codec: "hvc1", // H.265/HEVC base profile
      width,
      height,
      bitrate,
      framerate: fps,
      hardwareAcceleration: "prefer-hardware",
    });
    this.isOpen = true;
    console.log(
      `[H265Encoder] Successfully initialized with H.265: ${width}x${height} @ ${fps} FPS`,
    );
  }

  encode(frame: VideoFrame, forceKeyframe: boolean = false): void {
    if (!this.encoder || !this.isOpen) return;

    try {
      if (forceKeyframe) {
        this.encoder.encode(frame, { keyFrame: true });
      } else {
        this.encoder.encode(frame, { keyFrame: false });
      }
      this.frameCount++;
    } catch (err) {
      console.error("[H265Encoder] Failed to encode frame:", err);
      this.isOpen = false; // Mark encoder as closed on any error
    }
  }

  drain(): Array<{
    flags: number;
    width: number;
    height: number;
    data: Uint8Array;
  }> {
    const chunks = this.pendingChunks;
    this.pendingChunks = [];
    return chunks;
  }

  destroy(): void {
    this.isOpen = false;
    if (this.encoder) {
      this.encoder.close();
      this.encoder = null;
    }
    this.pendingChunks = [];
  }
}

const CAPABILITIES_PACKET_SIZE = 168; // Includes codec capabilities (added 8 bytes)
const STREAM_TYPE_VIDEO = 0x01;
const STREAM_TYPE_AUDIO = 0x02;

// Codec capability bitmasks (must match include/ascii-chat/media/codecs.h)
const VIDEO_CODEC_CAP_RGBA = 1 << 0; // Bit 0: RGBA support
const VIDEO_CODEC_CAP_H265 = 1 << 1; // Bit 1: H.265/HEVC support

// Only advertise H.265 if browser supports WebCodecs encoding
const VIDEO_CODEC_CAP_SUPPORTED = H265Encoder.isSupported()
  ? VIDEO_CODEC_CAP_RGBA | VIDEO_CODEC_CAP_H265
  : VIDEO_CODEC_CAP_RGBA;

const AUDIO_CODEC_CAP_RAW = 1 << 0; // Bit 0: Raw PCM support
const AUDIO_CODEC_CAP_OPUS = 1 << 1; // Bit 1: Opus support
const AUDIO_CODEC_CAP_ALL = AUDIO_CODEC_CAP_RAW | AUDIO_CODEC_CAP_OPUS;

// Helper functions to map Settings types to WASM enums
function mapColorMode(mode: string): ClientColorMode {
  const mapping: Record<string, ClientColorMode> = {
    auto: ClientColorMode.AUTO,
    none: ClientColorMode.NONE,
    "16": ClientColorMode.COLOR_16,
    "256": ClientColorMode.COLOR_256,
    truecolor: ClientColorMode.TRUECOLOR,
  };
  return mapping[mode] || ClientColorMode.AUTO;
}

function mapColorFilter(filter: string): ClientColorFilter {
  const mapping: Record<string, ClientColorFilter> = {
    none: ClientColorFilter.NONE,
    black: ClientColorFilter.BLACK,
    white: ClientColorFilter.WHITE,
    green: ClientColorFilter.GREEN,
    magenta: ClientColorFilter.MAGENTA,
    fuchsia: ClientColorFilter.FUCHSIA,
    orange: ClientColorFilter.ORANGE,
    teal: ClientColorFilter.TEAL,
    cyan: ClientColorFilter.CYAN,
    pink: ClientColorFilter.PINK,
    red: ClientColorFilter.RED,
    yellow: ClientColorFilter.YELLOW,
    rainbow: ClientColorFilter.RAINBOW,
  };
  return mapping[filter] || ClientColorFilter.NONE;
}

function buildStreamStartPacket(includeAudio: boolean = false): Uint8Array {
  const streamType = includeAudio
    ? STREAM_TYPE_VIDEO | STREAM_TYPE_AUDIO
    : STREAM_TYPE_VIDEO;
  const buf = new ArrayBuffer(4);
  const view = new DataView(buf);

  // Network byte order (big-endian)
  view.setUint32(0, streamType, false);

  const payload = new Uint8Array(buf);
  console.log(
    `[Client] STREAM_START payload: type=0x${streamType.toString(16)}, bytes=[${Array.from(
      payload,
    )
      .map((b) => `0x${b.toString(16).padStart(2, "0")}`)
      .join(" ")}]`,
  );
  return payload;
}

function buildCapabilitiesPacket(
  cols: number,
  rows: number,
  targetFps: number = 60,
): Uint8Array {
  const buf = new ArrayBuffer(CAPABILITIES_PACKET_SIZE);
  const view = new DataView(buf);
  const bytes = new Uint8Array(buf);

  // Network byte order (big-endian) - server uses NET_TO_HOST_U32/U16 to read
  view.setUint32(0, 0x0f, false); // capabilities (color+utf8+etc)
  view.setUint32(4, 3, false); // color_level (truecolor)
  view.setUint32(8, 16777216, false); // color_count
  view.setUint32(12, 0, false); // render_mode (foreground)
  view.setUint16(16, cols, false); // width
  view.setUint16(18, rows, false); // height

  // term_type[32] at offset 20
  const termType = new TextEncoder().encode("xterm-256color");
  bytes.set(termType, 20);

  // colorterm[32] at offset 52
  const colorterm = new TextEncoder().encode("truecolor");
  bytes.set(colorterm, 52);

  bytes[84] = 1; // detection_reliable
  view.setUint32(85, 1, false); // utf8_support
  view.setUint32(89, 1, false); // palette_type (PALETTE_STANDARD=1)
  // palette_custom[64] at offset 93 - zeroed
  bytes[157] = Math.min(targetFps, 144); // desired_fps (0-144)
  bytes[158] = 0; // color_filter (none)
  bytes[159] = 1; // wants_padding

  // Codec capabilities (network byte order, big-endian)
  // Offset 160-163: video codec capabilities (RGBA always, H.265 if browser supports)
  view.setUint32(160, VIDEO_CODEC_CAP_SUPPORTED, false);
  // Offset 164-167: audio codec capabilities (supports Raw PCM, Opus)
  view.setUint32(164, AUDIO_CODEC_CAP_ALL, false);

  // Log capabilities packet structure for debugging
  console.log(
    `[Client] CAPABILITIES packet: size=${bytes.length}, width=${cols}, height=${rows}, video_caps=0x${VIDEO_CODEC_CAP_SUPPORTED.toString(
      16,
    )}, audio_caps=0x${AUDIO_CODEC_CAP_ALL.toString(16)}`,
  );
  console.log(
    `[Client] CAPABILITIES first 32 bytes: [${Array.from(bytes.slice(0, 32))
      .map((b) => `0x${b.toString(16).padStart(2, "0")}`)
      .join(" ")}]`,
  );
  console.log(
    `[Client] CAPABILITIES last 8 bytes (codecs at offset 160-167): [${Array.from(
      bytes.slice(160, 168),
    )
      .map((b) => `0x${b.toString(16).padStart(2, "0")}`)
      .join(" ")}]`,
  );

  return bytes;
}

/**
 * Build IMAGE_FRAME payload: legacy 8-byte header + RGB24 pixel data.
 * Server expects: [width:4][height:4][rgb_data:w*h*3] (network byte order, big-endian)
 */
function buildImageFramePayload(
  rgbaData: Uint8Array,
  width: number,
  height: number,
): Uint8Array {
  const pixelCount = width * height;
  const rgb24Size = pixelCount * 3;
  // image_frame_packet_t structure:
  // width(4) + height(4) + pixel_format(4) + compressed_size(4) + checksum(4) + timestamp(4) + rgb24_data
  const headerSize = 24;
  const totalSize = headerSize + rgb24Size;
  const buf = new ArrayBuffer(totalSize);
  const view = new DataView(buf);
  const bytes = new Uint8Array(buf);

  // Fill header (network byte order big-endian)
  view.setUint32(0, width, false); // width
  view.setUint32(4, height, false); // height
  view.setUint32(8, 3, false); // pixel_format: 3 = RGB24
  view.setUint32(12, 0, false); // compressed_size: 0 (not compressed)
  view.setUint32(16, 0, false); // checksum: 0 (TODO: calculate proper CRC32 if needed)
  view.setUint32(20, Date.now(), false); // timestamp: current time in milliseconds

  // Convert RGBA to RGB24 (strip alpha channel)
  let srcIdx = 0;
  let dstIdx = headerSize;
  for (let i = 0; i < pixelCount; i++) {
    const r = rgbaData[srcIdx] ?? 0;
    const g = rgbaData[srcIdx + 1] ?? 0;
    const b = rgbaData[srcIdx + 2] ?? 0;
    bytes[dstIdx] = r; // R
    bytes[dstIdx + 1] = g; // G
    bytes[dstIdx + 2] = b; // B
    srcIdx += 4;
    dstIdx += 3;
  }

  return bytes;
}

/**
 * Build IMAGE_FRAME_H265 payload: H.265 encoded video frame.
 * Server expects: [flags:u8][width:u16 BE][height:u16 BE][h265_data...]
 * flags: 0x01 = KEYFRAME, 0x02 = SIZE_CHANGE
 */
function buildImageFrameH265Payload(
  flags: number,
  width: number,
  height: number,
  h265Data: Uint8Array,
): Uint8Array {
  const headerSize = 5; // 1 + 2 + 2 bytes
  const totalSize = headerSize + h265Data.byteLength;
  const buf = new ArrayBuffer(totalSize);
  const view = new DataView(buf);
  const bytes = new Uint8Array(buf);

  // Header: flags (1), width (2 BE), height (2 BE)
  view.setUint8(0, flags);
  view.setUint16(1, width, false); // big-endian
  view.setUint16(3, height, false); // big-endian

  // H.265 bitstream data
  bytes.set(h265Data, headerSize);

  return bytes;
}

const STATE_NAMES: Record<number, string> = {
  [ConnectionState.DISCONNECTED]: "Disconnected",
  [ConnectionState.CONNECTING]: "Connecting",
  [ConnectionState.HANDSHAKE]: "Performing handshake",
  [ConnectionState.CONNECTED]: "Connected",
  [ConnectionState.ERROR]: "Error",
};

export function ClientPage() {
  const rendererRef = useRef<AsciiRendererHandle>(null);
  const clientRef = useRef<ClientConnection | null>(null);
  const videoRef = useRef<HTMLVideoElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const streamRef = useRef<MediaStream | null>(null);
  const animationFrameRef = useRef<number | null>(null);
  const lastFrameTimeRef = useRef<number>(0);
  const frameIntervalRef = useRef<number>(1000 / 60); // 60 FPS
  const frameCountRef = useRef<number>(0);
  const receivedFrameCountRef = useRef<number>(0);
  const frameReceiptTimesRef = useRef<number[]>([]);
  const h265EncoderRef = useRef<H265Encoder | null>(null);

  const [status, setStatus] = useState<string>("Connecting...");
  const [publicKey, setPublicKey] = useState<string>("");
  const [connectionState, setConnectionState] = useState<ConnectionState>(
    ConnectionState.DISCONNECTED,
  );
  const [serverUrl, setServerUrl] = useState<string>("ws://localhost:27226");
  const [showModal, setShowModal] = useState(false);
  const [showSettings, setShowSettings] = useState(false);
  const [error, setError] = useState<string>("");
  const [terminalDimensions, setTerminalDimensions] = useState({
    cols: 0,
    rows: 0,
  });
  const [isWebcamRunning, setIsWebcamRunning] = useState(false);
  const [hasAutoConnected, setHasAutoConnected] = useState(false);
  const [fps, setFps] = useState<number | undefined>();
  const [wasmInitialized, setWasmInitialized] = useState(false);

  const optionsManager = useMemo(() => {
    if (!wasmInitialized || !isClientWasmReady()) return null;

    return createWasmOptionsManager(
      setColorMode,
      getColorMode,
      setColorFilter,
      getColorFilter,
      setPalette,
      getPalette,
      setPaletteChars,
      getPaletteChars,
      setMatrixRain,
      getMatrixRain,
      setFlipX,
      getFlipX,
      setDimensions,
      getDimensions,
      setTargetFps,
      getTargetFps,
      mapColorMode,
      mapColorFilter,
    );
  }, [wasmInitialized]);

  // Read server URL from query parameter (for E2E tests)
  // Use useLayoutEffect to ensure this runs before render and auto-connect
  useLayoutEffect(() => {
    const params = new URLSearchParams(window.location.search);
    const testServerUrl = params.get("testServerUrl");
    console.log(`[Client] Query params: search="${window.location.search}"`);
    console.log(`[Client] testServerUrl from query: "${testServerUrl}"`);
    if (testServerUrl) {
      console.log(`[Client] Setting serverUrl to: "${testServerUrl}"`);
      setServerUrl(testServerUrl);
    } else {
      console.log(
        "[Client] No testServerUrl in query, using default localhost:27226",
      );
    }
  }, []);

  // Settings state
  const [settings, setSettings] = useState<SettingsConfig>({
    width: 640,
    height: 480,
    targetFps: 60,
    colorMode: "truecolor",
    colorFilter: "none",
    palette: "standard",
    paletteChars: " =#░░▒▒▓▓██",
    matrixRain: false,
    flipX: false,
  });

  // Apply WASM settings when they change
  useEffect(() => {
    if (optionsManager && isClientWasmReady()) {
      try {
        optionsManager.applySettings(settings);
      } catch (err) {
        console.error("Failed to apply WASM settings:", err);
      }
    }
  }, [optionsManager, settings]);

  // Update frame interval when target FPS changes
  useEffect(() => {
    frameIntervalRef.current = 1000 / settings.targetFps;
  }, [settings.targetFps]);

  // Use shared canvas capture hook
  const { captureFrame } = useCanvasCapture(videoRef, canvasRef);

  const handleDimensionsChange = useCallback(
    (dims: { cols: number; rows: number }) => {
      setTerminalDimensions(dims);

      // Tell WASM about new dimensions (for proper ASCII rendering)
      if (optionsManager && isClientWasmReady()) {
        optionsManager.setDimensions(dims.cols, dims.rows);
      }

      // If connected, send updated dimensions to server
      if (clientRef.current && connectionState === ConnectionState.CONNECTED) {
        try {
          const payload = buildCapabilitiesPacket(
            dims.cols,
            dims.rows,
            settings.targetFps,
          );
          clientRef.current.sendPacket(PacketType.CLIENT_CAPABILITIES, payload);
        } catch (err) {
          console.error("[Client] Failed to send capabilities on resize:", err);
        }
      }
    },
    [connectionState, optionsManager, settings],
  );

  const connectToServer = useCallback(
    async (options?: { showErrors?: boolean }) => {
      const showErrors = options?.showErrors ?? true;
      try {
        console.log("[Client] connectToServer() called");
        console.log(`[Client] Server URL: ${serverUrl}`);
        console.log(
          `[Client] Terminal dimensions state: ${terminalDimensions.cols}x${terminalDimensions.rows}`,
        );
        console.log(`[Client] Starting connection attempt to: ${serverUrl}`);

        setStatus("Connecting...");
        if (showErrors) {
          setError("");
        }

        // Disconnect existing connection if any
        if (clientRef.current) {
          console.log("[Client] Disconnecting previous connection");
          clientRef.current.disconnect();
          clientRef.current = null;
        }

        const width = terminalDimensions.cols || 80;
        const height = terminalDimensions.rows || 40;
        console.log(
          `[Client] Creating ClientConnection with dimensions: ${width}x${height}`,
        );

        const conn = new ClientConnection({
          serverUrl,
          width,
          height,
        });

        conn.onStateChange((state) => {
          const stateName = STATE_NAMES[state] || "Unknown";
          console.log(`[Client] State change: ${state} (${stateName})`);

          setConnectionState(state);
          setStatus(stateName);

          if (state === ConnectionState.CONNECTED) {
            console.log(
              "[Client] CONNECTED state reached, attempting to send CLIENT_CAPABILITIES and STREAM_START",
            );

            // Check renderer dimensions
            const rendererDims = rendererRef.current?.getDimensions();
            console.log(
              `[Client] Renderer dimensions: ${
                rendererDims
                  ? `${rendererDims.cols}x${rendererDims.rows}`
                  : "null/undefined"
              }`,
            );
            console.log(
              `[Client] Renderer ref available: ${
                rendererRef.current ? "yes" : "no"
              }`,
            );

            // Send terminal capabilities after handshake
            // Use renderer dimensions if available, otherwise use defaults
            const dims =
              rendererDims && rendererDims.cols > 0
                ? rendererDims
                : { cols: 80, rows: 40 };
            console.log(
              `[Client] Using dimensions for capabilities: ${dims.cols}x${dims.rows}`,
            );

            // Send capabilities and stream start with retry logic since WASM state might not match React state immediately
            const sendSetupPackets = () => {
              try {
                console.log(
                  `[Client] Building CLIENT_CAPABILITIES packet (type=${PacketType.CLIENT_CAPABILITIES})`,
                );
                const capsPayload = buildCapabilitiesPacket(
                  dims.cols,
                  dims.rows,
                  settings.targetFps,
                );
                console.log(
                  `[Client] Payload size: ${capsPayload.length} bytes`,
                );
                console.log(`[Client] Sending CLIENT_CAPABILITIES packet...`);
                // Send as unencrypted ACIP packet (like native client does)
                // These are protocol control packets that must arrive before encryption fully setup
                conn.sendUnencryptedAcipPacket(
                  PacketType.CLIENT_CAPABILITIES,
                  capsPayload,
                );
                console.log(`[Client] CLIENT_CAPABILITIES sent successfully`);

                // Send STREAM_START to tell server we're about to send video
                console.log(
                  `[Client] Building STREAM_START packet (type=${PacketType.STREAM_START})`,
                );
                const streamPayload = buildStreamStartPacket(false); // Video only, no audio
                console.log(`[Client] Sending STREAM_START packet...`);
                // Send as unencrypted ACIP packet (like native client does)
                conn.sendUnencryptedAcipPacket(
                  PacketType.STREAM_START,
                  streamPayload,
                );
                console.log(`[Client] STREAM_START sent successfully`);
              } catch (err) {
                console.error("[Client] Failed to send setup packets:", err);
                // Retry after 100ms if it failed
                setTimeout(sendSetupPackets, 100);
              }
            };
            sendSetupPackets();
          }

          if (state === ConnectionState.ERROR) {
            console.error("[Client] Connection error state reached");
            if (showErrors) {
              setError("Connection error");
              setShowModal(true);
            }
          }
        });

        conn.onPacketReceived((parsed, decryptedPayload) => {
          if (parsed.type === PacketType.ASCII_FRAME) {
            receivedFrameCountRef.current++;
            const now = performance.now();
            frameReceiptTimesRef.current.push(now);

            // Update test metrics immediately (for E2E test frame counting)
            // Use unique frame count instead of packet count for accurate server frame measurement
            window.__clientFrameMetrics = {
              rendered: frameCountRef.current,
              received: Object.keys(uniqueReceivedFramesRef.current).length, // Count unique frames, not packets
              queueDepth: frameQueueRef.current.length,
              uniqueRendered: cumulativeUniqueFramesRef.current,
              frameHashes: uniqueReceivedFramesRef.current,
            };

            // Log unique frame arrival rate every time we see a new unique frame
            const uniqueCount = Object.keys(
              uniqueReceivedFramesRef.current,
            ).length;
            if (uniqueCount > 0 && uniqueCount % 1 === 0) {
              const recentTimes = frameReceiptTimesRef.current.slice(-10);
              if (recentTimes.length > 1) {
                const timeDiff =
                  recentTimes[recentTimes.length - 1]! - recentTimes[0]!;
                const packetsPerSecond = (9 / timeDiff) * 1000; // 9 intervals over 10 packets
                console.log(
                  `[Client] Frame arrival: ${packetsPerSecond.toFixed(
                    1,
                  )} packets/sec (${uniqueCount} unique frames, ${receivedFrameCountRef.current} total packets)`,
                );
              }
            }

            try {
              const frame = parseAsciiFrame(decryptedPayload);
              // Track unique frames at reception (for measuring actual frames from server)
              const frameHash = hashFrame(frame.ansiString);
              if (!uniqueReceivedFramesRef.current[frameHash]) {
                uniqueReceivedFramesRef.current[frameHash] = 0;
              }
              uniqueReceivedFramesRef.current[frameHash]++;

              // Queue frame for the render loop to process at target FPS
              // This prevents frame accumulation when tab is hidden
              frameQueueRef.current.push(frame.ansiString);
              console.log("ASCII_FRAME PACKET RECEIVED");
            } catch (err) {
              console.error("[Client] Failed to parse ASCII frame:", err);
            }
          }
        });

        console.log("[Client] Calling conn.connect()...");
        await conn.connect();
        console.log("[Client] conn.connect() completed successfully");

        // Only mark WASM as initialized AFTER connection and WASM init complete
        setWasmInitialized(true);

        clientRef.current = conn;
        const pubKey = conn.getPublicKey() || "";
        console.log(`[Client] Public key set: ${pubKey.substring(0, 20)}...`);
        setPublicKey(pubKey);
      } catch (err) {
        const errMsg = `${err}`;
        console.error("[Client] Connection failed:", errMsg);
        console.error("[Client] Error object:", err);
        console.error(
          "[Client] Error stack:",
          err instanceof Error ? err.stack : "unknown",
        );
        if (showErrors) {
          setStatus(`Error: ${errMsg}`);
          setError(errMsg);
          setShowModal(true);
        }
        throw err; // Re-throw so auto-connect can retry
      }
    },
    [serverUrl, terminalDimensions, settings],
  );

  const handleDisconnect = () => {
    console.log("[Client] handleDisconnect() called");
    stopWebcam();

    if (clientRef.current) {
      console.log("[Client] Disconnecting from server");
      clientRef.current.disconnect();
      clientRef.current = null;
    }

    console.log("[Client] Clearing connection state");
    setConnectionState(ConnectionState.DISCONNECTED);
    setStatus("Disconnected");
    setPublicKey("");
  };

  // Render loop for displaying received frames at target FPS (decoupled from network arrival rate)
  const frameQueueRef = useRef<string[]>([]);

  const renderLoopStartTimeRef = useRef<number>(0);

  const renderCallCountRef = useRef(0);
  const frameHashesRef = useRef<Record<string, number>>({});

  // Simple hash function for frame content
  const hashFrame = (content: string): string => {
    let hash = 0;
    for (let i = 0; i < content.length; i += 10) {
      hash = (hash << 5) - hash + content.charCodeAt(i);
      hash = hash & hash; // Convert to 32bit integer
    }
    return hash.toString(36);
  };

  const renderNoOpCountRef = useRef(0);
  const diagnosticFrameCountRef = useRef(0);
  const cumulativeUniqueFramesRef = useRef(0);
  const uniqueReceivedFramesRef = useRef<Record<string, number>>({}); // Track unique frames at reception

  // Expose frame count for testing
  useEffect(() => {
    const metrics = {
      rendered: frameCountRef.current,
      received: Object.keys(uniqueReceivedFramesRef.current).length, // Count unique frames, not packets
      queueDepth: frameQueueRef.current.length,
      uniqueRendered: cumulativeUniqueFramesRef.current,
      frameHashes: uniqueReceivedFramesRef.current,
    };
    window.__clientFrameMetrics = metrics;
    if (frameCountRef.current % 60 === 0 && frameCountRef.current > 0) {
      console.log("[Client] Exposed metrics:", metrics);
    }
  });

  const renderFrame = useCallback(() => {
    renderCallCountRef.current++;

    if (frameQueueRef.current.length === 0 || !rendererRef.current) {
      renderNoOpCountRef.current++;
    }

    // Log every 60 calls to renderFrame (regardless of whether we actually render)
    if (renderCallCountRef.current % 60 === 0) {
      const noOpRate = (
        (renderNoOpCountRef.current / renderCallCountRef.current) *
        100
      ).toFixed(1);
      console.log(
        `[Client] renderFrame called ${renderCallCountRef.current} times (${noOpRate}% no-op), queue depth: ${frameQueueRef.current.length}`,
      );
    }

    // Render one frame per RAF callback to maintain display sync with server
    if (frameQueueRef.current.length > 0 && rendererRef.current) {
      if (renderLoopStartTimeRef.current === 0) {
        renderLoopStartTimeRef.current = performance.now();
      }

      // Render frames in FIFO order (first in, first out)
      // This ensures we display the video stream in proper sequence
      const frameContent = frameQueueRef.current.shift()!;

      const frameHash = hashFrame(frameContent);
      // Track if this is a new unique frame we haven't seen before
      if (!frameHashesRef.current[frameHash]) {
        cumulativeUniqueFramesRef.current++;
      }
      frameHashesRef.current[frameHash] =
        (frameHashesRef.current[frameHash] || 0) + 1;

      rendererRef.current.writeFrame(frameContent);

      frameCountRef.current++;
      diagnosticFrameCountRef.current++;

      // Log render rate every 60 rendered frames (using diagnostic counter)
      if (diagnosticFrameCountRef.current % 60 === 0) {
        const uniqueFrames = Object.keys(frameHashesRef.current).length;
        console.log(`[Client] Rendered ${uniqueFrames} unique frames`);
        console.log(
          `[Client] Frame hash distribution:`,
          frameHashesRef.current,
        );
        renderLoopStartTimeRef.current = performance.now();
        diagnosticFrameCountRef.current = 0;
        frameHashesRef.current = {};
      }
    }
  }, []);

  const { startRenderLoop } = useRenderLoop(
    renderFrame,
    frameIntervalRef,
    lastFrameTimeRef,
  );

  // Webcam capture loop - must avoid stale closures in RAF recursion
  const webcamCaptureLoopRef = useRef<(() => void) | null>(null);

  // Helper to compute simple frame hash
  const computeFrameHash = (data: Uint8Array): number => {
    let hash = 0;
    // Sample every 256th byte for speed
    for (let i = 0; i < data.length; i += 256) {
      hash = (hash << 5) - hash + (data[i] ?? 0);
      hash = hash & hash;
    }
    return Math.abs(hash);
  };

  // Inner loop function that doesn't have dependencies - this prevents RAF recursion from breaking
  const captureLoopCountRef = useRef(0);
  const captureLoopFrameCountRef = useRef(0);
  const lastFrameHashRef = useRef(0);
  const uniqueFrameCountRef = useRef(0);
  const videFrameUpdateCountRef = useRef(0); // Track actual VIDEO updates
  const captureTimerRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const createWebcamCaptureLoop = useCallback(() => {
    let lastLogTime = performance.now();

    // Timer-based frame sending to match server render loop (not RAF-based)
    // RAF fires at monitor refresh rate (60+ Hz) regardless of frame send interval
    // Timer ensures we send at exactly the target FPS, matching C client behavior
    const sendOneFrame = () => {
      const now = performance.now();

      // Log every 100ms regardless of frame sends
      if (now - lastLogTime > 100) {
        lastLogTime = now;
        console.log(
          `[Client] Frame send: count=${captureLoopFrameCountRef.current}, unique=${uniqueFrameCountRef.current}, video_updates=${videFrameUpdateCountRef.current}, ready=${
            !!clientRef.current && connectionState === ConnectionState.CONNECTED
          }`,
        );
      }

      // Call captureAndSendFrame through ref to get the latest version
      const conn = clientRef.current;
      if (conn && connectionState === ConnectionState.CONNECTED) {
        const frame = captureFrame();
        if (frame && frame.data) {
          captureLoopFrameCountRef.current++;
          const frameHash = computeFrameHash(frame.data);

          // Log EVERY frame sent, not just unique ones
          const isNewFrame = frameHash !== lastFrameHashRef.current;
          if (isNewFrame) {
            uniqueFrameCountRef.current++;
            videFrameUpdateCountRef.current++;
            lastFrameHashRef.current = frameHash;
            // console.log(
            //   `[Client] SEND #${captureLoopFrameCountRef.current} (UNIQUE #${uniqueFrameCountRef.current}): hash=0x${frameHash.toString(
            //     16,
            //   )}, size=${frame.data.length}`,
            // );
          } else {
            // console.log(
            //   `[Client] SEND #${captureLoopFrameCountRef.current} (DUPLICATE): hash=0x${frameHash.toString(
            //     16,
            //   )}, size=${frame.data.length}`,
            // );
          }

          // Try H.265 encoding if available (but prioritize RGBA for stability)
          // H.265 encoding can be slow on some systems, so we always have RGBA fallback
          // Set window.DISABLE_H265 = true in console to disable H.265 encoding
          const h265Disabled =
            typeof window !== "undefined" &&
            (window as unknown as Record<string, unknown>)["DISABLE_H265"] ===
              true;
          let sentH265 = false;
          if (
            !h265Disabled &&
            h265EncoderRef.current &&
            H265Encoder.isSupported()
          ) {
            try {
              if (!canvasRef.current) {
                throw new Error("Canvas not available for VideoFrame creation");
              }

              // Create VideoFrame from canvas for H.265 encoding
              const videoFrame = new VideoFrame(canvasRef.current, {
                timestamp: now * 1000, // microseconds
              });

              // Request keyframe every 60 frames
              const forceKeyframe = captureLoopFrameCountRef.current % 60 === 0;
              h265EncoderRef.current.encode(videoFrame, forceKeyframe);
              videoFrame.close();

              // Send any available encoded chunks (may be empty since encoder is async)
              const chunks = h265EncoderRef.current.drain();
              if (chunks.length > 0) {
                for (const chunk of chunks) {
                  const payload = buildImageFrameH265Payload(
                    chunk.flags,
                    chunk.width,
                    chunk.height,
                    chunk.data,
                  );
                  conn.sendPacket(PacketType.IMAGE_FRAME_H265, payload);
                }
                sentH265 = true;
              }
            } catch (err) {
              console.error(
                "[Client] H.265 encoding failed, will use RGBA:",
                err,
              );
              // Disable H.265 for rest of session if encoding fails
              if (h265EncoderRef.current) {
                h265EncoderRef.current.destroy();
                h265EncoderRef.current = null;
              }
            }
          }

          // Always send RGBA if H.265 didn't produce chunks or failed
          if (!sentH265) {
            const payload = buildImageFramePayload(
              frame.data,
              frame.width,
              frame.height,
            );

            try {
              conn.sendPacket(PacketType.IMAGE_FRAME, payload);
            } catch (err) {
              console.error("[Client] Failed to send IMAGE_FRAME:", err);
            }
          }
        } else {
          console.warn(
            `[Client] captureFrame returned null at call ${captureLoopCountRef.current}`,
          );
        }
      }
    };

    return sendOneFrame;
  }, [captureFrame, connectionState]);

  // Create capture function ref
  useEffect(() => {
    webcamCaptureLoopRef.current = createWebcamCaptureLoop();
  }, [createWebcamCaptureLoop]);

  // Start/stop timer when connection changes
  useEffect(() => {
    if (
      connectionState === ConnectionState.CONNECTED &&
      webcamCaptureLoopRef.current
    ) {
      // Start timer to send frames at target FPS
      const sendInterval = 1000 / settings.targetFps;
      captureTimerRef.current = setInterval(() => {
        if (webcamCaptureLoopRef.current) {
          webcamCaptureLoopRef.current();
        }
      }, sendInterval);
      console.log(
        `[Client] Started frame send timer: ${sendInterval.toFixed(1)}ms interval (${settings.targetFps} FPS)`,
      );
    } else {
      // Stop timer when disconnected
      if (captureTimerRef.current) {
        clearInterval(captureTimerRef.current);
        captureTimerRef.current = null;
        console.log("[Client] Stopped frame send timer");
      }
    }

    return () => {
      if (captureTimerRef.current) {
        clearInterval(captureTimerRef.current);
        captureTimerRef.current = null;
      }
    };
  }, [connectionState, settings.targetFps]);

  const startWebcam = useCallback(async () => {
    console.log("[Client] startWebcam() called");
    console.log(
      `[DEBUG] videoRef.current=${!!videoRef.current}, canvasRef.current=${!!canvasRef.current}`,
    );

    if (!videoRef.current || !canvasRef.current) {
      console.error("[Client] Video or canvas element not ready");
      setError("Video or canvas element not ready");
      return;
    }

    console.log(
      `[DEBUG] connectionState=${connectionState} vs CONNECTED=${ConnectionState.CONNECTED}`,
    );
    if (connectionState !== ConnectionState.CONNECTED) {
      console.error(
        `[Client] Not connected (state=${connectionState}), cannot start webcam`,
      );
      setError("Must be connected to server before starting webcam");
      return;
    }

    console.log("[Client] Passed all initial checks");

    try {
      // Send STREAM_START to notify server we're about to send video
      if (clientRef.current) {
        console.log("[Client] Sending STREAM_START before webcam...");
        const streamPayload = buildStreamStartPacket(false);
        // Send as unencrypted ACIP packet (like native client does)
        clientRef.current.sendUnencryptedAcipPacket(
          PacketType.STREAM_START,
          streamPayload,
        );
        console.log("[Client] STREAM_START sent");
      } else {
        console.log(
          "[Client] clientRef.current is null, skipping STREAM_START",
        );
      }

      const w = settings.width || 1280;
      const h = settings.height || 720;
      console.log(`[Client] Requesting webcam stream: ${w}x${h}`);
      console.log(
        `[DEBUG] videoRef.current before getUserMedia:`,
        videoRef.current,
      );

      let stream;
      try {
        stream = await navigator.mediaDevices.getUserMedia({
          video: { width: { ideal: w }, height: { ideal: h } },
          audio: false,
        });
      } catch (err) {
        console.error(
          "[Client] getUserMedia failed (trying fallback without constraints):",
          err,
        );
        try {
          stream = await navigator.mediaDevices.getUserMedia({
            video: true,
            audio: false,
          });
        } catch (err2) {
          console.error("[Client] getUserMedia failed completely:", err2);
          throw err2;
        }
      }

      console.log("[Client] Webcam stream acquired");
      console.log(`[DEBUG] Stream object:`, stream);
      console.log(
        `[Client] Stream tracks: ${stream.getTracks().length}, active=${stream.active}`,
      );

      // Log track details
      stream.getTracks().forEach((track, idx) => {
        console.log(`[DEBUG] Track ${idx}:`, {
          kind: track.kind,
          enabled: track.enabled,
          readyState: track.readyState,
          label: track.label,
          settings: track.getSettings ? track.getSettings() : "N/A",
        });
      });

      streamRef.current = stream;
      const video = videoRef.current!;
      console.log("[DEBUG] Before setting srcObject, video element:", {
        videoWidth: video.videoWidth,
        videoHeight: video.videoHeight,
        readyState: video.readyState,
        networkState: video.networkState,
      });

      video.srcObject = stream;
      console.log("[DEBUG] After setting srcObject");
      console.log("[DEBUG] Video element after srcObject:", {
        videoWidth: video.videoWidth,
        videoHeight: video.videoHeight,
        srcObject: !!video.srcObject,
      });

      // Monitor stream for unexpected end
      stream.getTracks().forEach((track) => {
        track.onended = () => {
          console.warn(
            `[Client] Media track ended (${track.kind}): readyState=${track.readyState}`,
          );
        };
        track.onmute = () => {
          console.warn(`[Client] Media track muted (${track.kind})`);
        };
        track.onunmute = () => {
          console.log(`[Client] Media track unmuted (${track.kind})`);
        };
      });

      // Set up metadata listener BEFORE playing to catch the event
      const metadataPromise = new Promise<void>((resolve) => {
        const handleMetadata = () => {
          const video = videoRef.current!;
          const canvas = canvasRef.current!;
          console.log(
            `[Client] Webcam metadata loaded: ${video.videoWidth}x${video.videoHeight}, videoTime=${video.currentTime}, paused=${video.paused}`,
          );
          console.log("[DEBUG] Metadata event - full video element state:", {
            videoWidth: video.videoWidth,
            videoHeight: video.videoHeight,
            readyState: video.readyState,
            networkState: video.networkState,
            currentTime: video.currentTime,
            duration: video.duration,
            paused: video.paused,
            srcObject: !!video.srcObject,
            src: video.src,
          });

          // Check if dimensions are valid before resizing
          if (video.videoWidth > 0 && video.videoHeight > 0) {
            canvas.width = video.videoWidth;
            canvas.height = video.videoHeight;
            console.log(
              `[Client] Canvas resized to: ${canvas.width}x${canvas.height}`,
            );
            console.log("[DEBUG] Canvas state after resize:", {
              width: canvas.width,
              height: canvas.height,
              clientWidth: canvas.clientWidth,
              clientHeight: canvas.clientHeight,
            });
          } else {
            console.warn(
              `[DEBUG] Invalid video dimensions: ${video.videoWidth}x${video.videoHeight}, not resizing canvas`,
            );
          }
          video.removeEventListener("loadedmetadata", handleMetadata);
          resolve();
        };
        videoRef.current!.addEventListener("loadedmetadata", handleMetadata);
        console.log("[DEBUG] loadedmetadata listener attached");
      });

      // Now play the video (metadata event may already be queued)
      console.log("[Client] Attempting to play video...");
      console.log("[DEBUG] Video element before play():", {
        videoWidth: video.videoWidth,
        videoHeight: video.videoHeight,
        readyState: video.readyState,
        paused: video.paused,
        srcObject: !!video.srcObject,
      });
      try {
        await video.play();
        console.log("[Client] Video is playing");
        console.log("[DEBUG] Video element after play():", {
          videoWidth: video.videoWidth,
          videoHeight: video.videoHeight,
          readyState: video.readyState,
          paused: video.paused,
        });
      } catch (playErr) {
        console.error("[Client] Video play failed (may be expected):", playErr);
        console.error("[DEBUG] Video state when play() failed:", {
          videoWidth: video.videoWidth,
          videoHeight: video.videoHeight,
          readyState: video.readyState,
          srcObject: !!video.srcObject,
        });
      }

      // Wait for metadata with a timeout (5 seconds) in case it never fires
      const timeoutPromise = new Promise<void>((resolve) => {
        setTimeout(() => {
          console.warn(
            "[Client] Metadata timeout - setting canvas dimensions from current video properties",
          );
          if (videoRef.current && canvasRef.current) {
            const video = videoRef.current;
            const canvas = canvasRef.current;
            if (video.videoWidth > 0 && video.videoHeight > 0) {
              canvas.width = video.videoWidth;
              canvas.height = video.videoHeight;
              console.log(
                `[Client] Canvas resized from timeout: ${canvas.width}x${canvas.height}`,
              );
            }
          }
          resolve();
        }, 5000);
      });

      await Promise.race([metadataPromise, timeoutPromise]);

      // Validate that canvas has valid dimensions before proceeding
      if (
        !canvasRef.current ||
        canvasRef.current.width === 0 ||
        canvasRef.current.height === 0
      ) {
        const video = videoRef.current;
        console.warn(
          `[startWebcam] Canvas dimensions still invalid after metadata wait: canvas=${canvasRef.current?.width}x${canvasRef.current?.height}, video=${video?.videoWidth}x${video?.videoHeight}`,
        );
        // If video has dimensions, use them as fallback
        if (video && video.videoWidth > 0 && video.videoHeight > 0) {
          if (canvasRef.current) {
            canvasRef.current.width = video.videoWidth;
            canvasRef.current.height = video.videoHeight;
            console.log(
              `[startWebcam] Using video dimensions as fallback: ${video.videoWidth}x${video.videoHeight}`,
            );
          }
        } else {
          // Video still has no dimensions - cannot proceed
          throw new Error(
            "Failed to obtain video dimensions after 5-second wait. Browser may not have granted camera permissions or device is not available.",
          );
        }
      }

      console.log(
        `[startWebcam] About to start capture loop, videoRef=${
          videoRef.current ? "OK" : "NULL"
        }, canvasRef=${canvasRef.current ? "OK" : "NULL"}`,
      );
      if (videoRef.current) {
        console.log(
          `[startWebcam] Video: playing=${!videoRef.current
            .paused}, width=${videoRef.current.videoWidth}, height=${videoRef.current.videoHeight}`,
        );
      }

      setIsWebcamRunning(true);
      lastFrameTimeRef.current = performance.now();
      frameIntervalRef.current = 1000 / settings.targetFps;
      frameQueueRef.current = [];

      // Initialize H.265 encoder if supported
      if (H265Encoder.isSupported() && canvasRef.current && videoRef.current) {
        try {
          const w = canvasRef.current.width || 1280;
          const h = canvasRef.current.height || 720;
          console.log(
            `[Client] Initializing H.265 encoder: ${w}x${h} @ ${settings.targetFps} FPS`,
          );
          h265EncoderRef.current = new H265Encoder();
          await h265EncoderRef.current.initialize(w, h, settings.targetFps);
          console.log("[Client] H.265 encoder initialized successfully");
        } catch (err) {
          console.error("[Client] H.265 encoder initialization failed:", err);
          h265EncoderRef.current?.destroy();
          h265EncoderRef.current = null;
        }
      } else if (!H265Encoder.isSupported()) {
        console.log(
          "[Client] H.265 encoding not supported in this browser, using RGBA fallback",
        );
      }

      console.log("[Client] Starting render loops...");
      console.log(
        `[Client] frameInterval set to: ${frameIntervalRef.current}ms (${settings.targetFps} FPS)`,
      );

      // Log stream state before starting capture
      if (streamRef.current) {
        console.log(
          `[Client] Stream state before capture: active=${streamRef.current.active}, tracks=${streamRef.current.getTracks().length}`,
        );
        streamRef.current.getTracks().forEach((track) => {
          console.log(
            `[Client] Track (${track.kind}): readyState=${track.readyState}, enabled=${track.enabled}, muted=${track.muted}`,
          );
        });
      }

      // Start both the capture loop (sends to server) and render loop (displays frames)
      if (webcamCaptureLoopRef.current) {
        animationFrameRef.current = requestAnimationFrame(
          webcamCaptureLoopRef.current,
        );
        console.log("[Client] Webcam capture loop started");
      }
      console.log("[Client] Webcam started successfully");
    } catch (err) {
      const errMsg = `Failed to start webcam: ${err}`;
      console.error("[Client]", errMsg);
      console.error("[Client] Error:", err);
      setError(errMsg);
    }
  }, [connectionState, settings.width, settings.height, settings.targetFps]);

  const stopWebcam = useCallback(() => {
    // Stop timer (connection state change will also stop it)
    if (captureTimerRef.current) {
      clearInterval(captureTimerRef.current);
      captureTimerRef.current = null;
    }

    if (streamRef.current) {
      streamRef.current.getTracks().forEach((track) => track.stop());
      streamRef.current = null;
    }

    if (videoRef.current) {
      videoRef.current.srcObject = null;
    }

    // Clean up H.265 encoder
    if (h265EncoderRef.current) {
      h265EncoderRef.current.destroy();
      h265EncoderRef.current = null;
    }

    frameQueueRef.current = [];
    setIsWebcamRunning(false);
  }, []);

  // Auto-connect on mount with serverUrl
  // Continuously retry connection with exponential backoff until successful
  // This allows page load before server startup, silently retrying in background
  const reconnectAttemptRef = useRef<number>(0);
  const reconnectTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(
    null,
  );

  useEffect(() => {
    if (hasAutoConnected) return;

    const attemptConnect = async () => {
      reconnectAttemptRef.current++;
      const attempt = reconnectAttemptRef.current;
      console.log(
        `[Client] Auto-connect attempt ${attempt} with serverUrl:`,
        serverUrl,
      );

      try {
        // Use showErrors: false to suppress modal and let retries happen silently
        await connectToServer({ showErrors: false });
        console.log("[Client] Auto-connect succeeded on attempt:", attempt);
        setHasAutoConnected(true);
      } catch (err) {
        const delayMs = Math.min(1000, 100 * attempt); // Backoff: 100ms, 200ms, ..., up to 1000ms
        console.log(
          `[Client] Auto-connect failed on attempt ${attempt}, retrying in ${delayMs}ms:`,
          err,
        );

        // Schedule next retry
        reconnectTimeoutRef.current = setTimeout(attemptConnect, delayMs);
      }
    };

    // Initial 150ms delay to ensure terminal sizing is complete
    const initialTimer = setTimeout(attemptConnect, 150);

    return () => {
      clearTimeout(initialTimer);
      if (reconnectTimeoutRef.current) {
        clearTimeout(reconnectTimeoutRef.current);
        reconnectTimeoutRef.current = null;
      }
    };
  }, [serverUrl, hasAutoConnected, connectToServer]);

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      console.log("[Client] Component unmounting");
      stopWebcam();
      if (clientRef.current) {
        clientRef.current.disconnect();
        clientRef.current = null;
      }
      cleanupClientWasm();
    };
  }, [stopWebcam]);

  // Auto-start webcam once connected
  // Start render loop when connected (independent of webcam)
  useEffect(() => {
    if (connectionState === ConnectionState.CONNECTED) {
      console.log(
        "[Client] Connected, starting render loop for server frames...",
      );
      startRenderLoop();
    }
    // Note: startRenderLoop is NOT in deps array to avoid circular dependency issues
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [connectionState]);

  useEffect(() => {
    if (connectionState === ConnectionState.CONNECTED && !isWebcamRunning) {
      console.log("[Client] Connected and ready, auto-starting webcam...");
      startWebcam();
    }
    // Note: startWebcam is NOT in deps array to avoid circular dependency issues
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [connectionState, isWebcamRunning]);

  const getStatusDotColor = () => {
    switch (connectionState) {
      case ConnectionState.CONNECTED:
        return "bg-terminal-2";
      case ConnectionState.CONNECTING:
      case ConnectionState.HANDSHAKE:
        return "bg-terminal-3";
      case ConnectionState.ERROR:
        return "bg-terminal-1";
      default:
        return "bg-terminal-8";
    }
  };

  return (
    <>
      <WebClientHead
        title="Client - ascii-chat Web Client"
        description="Connect to an ascii-chat server. Real-time encrypted video chat rendered as ASCII art in your browser."
        url="https://web.ascii-chat.com/client"
      />
      <PageLayout
        videoRef={videoRef}
        canvasRef={canvasRef}
        showSettings={showSettings}
        settingsPanel={
          <Settings
            config={settings}
            onChange={setSettings}
            mode={AsciiChatMode.CLIENT}
          />
        }
        controlBar={
          <PageControlBar
            title="Client"
            status={status}
            statusDotColor={getStatusDotColor()}
            dimensions={terminalDimensions}
            fps={fps}
            targetFps={settings.targetFps}
            isWebcamRunning={isWebcamRunning}
            onStartWebcam={
              connectionState === ConnectionState.CONNECTED
                ? startWebcam
                : undefined
            }
            onStopWebcam={isWebcamRunning ? stopWebcam : undefined}
            showConnectionButton={true}
            onConnectionClick={() => setShowModal(true)}
            onSettingsClick={() => setShowSettings(!showSettings)}
            showSettingsButton={true}
          />
        }
        renderer={
          <AsciiRenderer
            ref={rendererRef}
            onDimensionsChange={handleDimensionsChange}
            onFpsChange={setFps}
            error={error}
            showFps={isWebcamRunning}
          />
        }
        modal={
          <ConnectionPanelModal
            isOpen={showModal}
            onClose={() => setShowModal(false)}
            connectionState={connectionState}
            status={status}
            publicKey={publicKey}
            serverUrl={serverUrl}
            onServerUrlChange={setServerUrl}
            onConnect={connectToServer}
            onDisconnect={handleDisconnect}
            isConnected={connectionState === ConnectionState.CONNECTED}
          />
        }
      />
    </>
  );
}
