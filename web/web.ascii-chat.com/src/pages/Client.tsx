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

const CAPABILITIES_PACKET_SIZE = 160;
const STREAM_TYPE_VIDEO = 0x01;
const STREAM_TYPE_AUDIO = 0x02;

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

  return new Uint8Array(buf);
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
                conn.sendPacket(PacketType.CLIENT_CAPABILITIES, capsPayload);
                console.log(`[Client] CLIENT_CAPABILITIES sent successfully`);

                // Send STREAM_START to tell server we're about to send video
                console.log(
                  `[Client] Building STREAM_START packet (type=${PacketType.STREAM_START})`,
                );
                const streamPayload = buildStreamStartPacket(false); // Video only, no audio
                console.log(`[Client] Sending STREAM_START packet...`);
                conn.sendPacket(PacketType.STREAM_START, streamPayload);
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

            // Log frame arrival rate every 10 frames
            if (receivedFrameCountRef.current % 10 === 0) {
              const recentTimes = frameReceiptTimesRef.current.slice(-10);
              if (recentTimes.length > 1) {
                const timeDiff =
                  recentTimes[recentTimes.length - 1]! - recentTimes[0]!;
                const framesPerSecond = (9 / timeDiff) * 1000; // 9 intervals over 10 frames
                console.log(
                  `[Client] Frame arrival rate: ${framesPerSecond.toFixed(
                    1,
                  )} FPS (received ${receivedFrameCountRef.current} total frames)`,
                );
              }
            }

            try {
              const frame = parseAsciiFrame(decryptedPayload);
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

  // Expose frame count for testing
  useEffect(() => {
    const metrics = {
      rendered: frameCountRef.current,
      received: receivedFrameCountRef.current,
      queueDepth: frameQueueRef.current.length,
      uniqueRendered: cumulativeUniqueFramesRef.current,
      frameHashes: frameHashesRef.current,
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
        const now = performance.now();
        const elapsed = now - renderLoopStartTimeRef.current;
        const renderFps = (diagnosticFrameCountRef.current / elapsed) * 1000;
        const uniqueFrames = Object.keys(frameHashesRef.current).length;
        console.log(
          `[Client] Rendered ${diagnosticFrameCountRef.current} frames in ${elapsed.toFixed(
            0,
          )}ms = ${renderFps.toFixed(
            1,
          )} FPS (${uniqueFrames} unique frame hashes)`,
        );
        console.log(
          `[Client] Frame hash distribution:`,
          frameHashesRef.current,
        );
        renderLoopStartTimeRef.current = now;
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
  const createWebcamCaptureLoop = useCallback(() => {
    let sendFrameTimeRef = performance.now();
    let lastLogTime = performance.now();
    return () => {
      captureLoopCountRef.current++;
      const now = performance.now();
      const elapsed = now - sendFrameTimeRef;
      const sendInterval = 1000 / settings.targetFps; // Send at target FPS

      // Log every 100ms regardless of frame sends
      if (now - lastLogTime > 100) {
        lastLogTime = now;
        console.log(
          `[Client] RAF cycle: calls=${captureLoopCountRef.current}, frames_sent=${captureLoopFrameCountRef.current}, unique=${uniqueFrameCountRef.current}, video_updates=${videFrameUpdateCountRef.current}, ready=${
            !!clientRef.current && connectionState === ConnectionState.CONNECTED
          }`,
        );
      }

      if (elapsed >= sendInterval) {
        sendFrameTimeRef = now;
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
              console.log(
                `[Client] SEND #${captureLoopFrameCountRef.current} (UNIQUE #${uniqueFrameCountRef.current}): hash=0x${frameHash.toString(
                  16,
                )}, size=${frame.data.length}`,
              );
            } else {
              console.log(
                `[Client] SEND #${captureLoopFrameCountRef.current} (DUPLICATE): hash=0x${frameHash.toString(
                  16,
                )}, size=${frame.data.length}`,
              );
            }

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
          } else {
            console.warn(
              `[Client] captureFrame returned null at call ${captureLoopCountRef.current}`,
            );
          }
        }
      }

      // Schedule next frame
      if (webcamCaptureLoopRef.current) {
        animationFrameRef.current = requestAnimationFrame(
          webcamCaptureLoopRef.current,
        );
      }
    };
  }, [captureFrame, connectionState, settings.targetFps]);

  // Update the ref whenever dependencies change (including connectionState)
  useEffect(() => {
    webcamCaptureLoopRef.current = createWebcamCaptureLoop();
  }, [createWebcamCaptureLoop]);

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
        clientRef.current.sendPacket(PacketType.STREAM_START, streamPayload);
        console.log("[Client] STREAM_START sent");
      } else {
        console.log(
          "[Client] clientRef.current is null, skipping STREAM_START",
        );
      }

      const w = settings.width || 1280;
      const h = settings.height || 720;
      console.log(`[Client] Requesting webcam stream: ${w}x${h}`);

      const stream = await navigator.mediaDevices.getUserMedia({
        video: {
          width: { ideal: w },
          height: { ideal: h },
          facingMode: "user",
        },
        audio: false,
      });

      console.log("[Client] Webcam stream acquired");
      streamRef.current = stream;
      const video = videoRef.current!;
      video.srcObject = stream;

      // Set up metadata listener BEFORE playing to catch the event
      const metadataPromise = new Promise<void>((resolve) => {
        const handleMetadata = () => {
          const video = videoRef.current!;
          const canvas = canvasRef.current!;
          console.log(
            `[Client] Webcam metadata loaded: ${video.videoWidth}x${video.videoHeight}, videoTime=${video.currentTime}, paused=${video.paused}`,
          );
          canvas.width = video.videoWidth;
          canvas.height = video.videoHeight;
          console.log(
            `[Client] Canvas resized to: ${canvas.width}x${canvas.height}`,
          );
          video.removeEventListener("loadedmetadata", handleMetadata);
          resolve();
        };
        videoRef.current!.addEventListener("loadedmetadata", handleMetadata);
      });

      // Now play the video (metadata event may already be queued)
      console.log("[Client] Attempting to play video...");
      try {
        await video.play();
        console.log("[Client] Video is playing");
      } catch (playErr) {
        console.error("[Client] Video play failed (may be expected):", playErr);
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

      console.log("[Client] Starting render loops...");
      console.log(
        `[Client] frameInterval set to: ${frameIntervalRef.current}ms (${settings.targetFps} FPS)`,
      );

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
    if (animationFrameRef.current !== null) {
      cancelAnimationFrame(animationFrameRef.current);
      animationFrameRef.current = null;
    }

    if (streamRef.current) {
      streamRef.current.getTracks().forEach((track) => track.stop());
      streamRef.current = null;
    }

    if (videoRef.current) {
      videoRef.current.srcObject = null;
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
