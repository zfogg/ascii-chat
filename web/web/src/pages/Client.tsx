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
  ConnectionState,
  isWasmReady as isClientWasmReady,
  PacketType,
  getClientModule,
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
} from "@ascii-chat/shared/wasm";
import {
  AsciiRenderer,
  ConnectionPanelModal,
  Settings,
  AsciiChatWebHead,
  PageControlBar,
  PageLayout,
} from "../components";
import type { AsciiRendererHandle, SettingsConfig } from "../components";
import {
  AsciiChatMode,
  mapColorModeToClient,
  mapColorFilterToClient,
  DEFAULT_SETTINGS,
} from "../utils";
import { DISCOVERY_SERVICE_URL, SITES } from "@ascii-chat/shared/utils";
import {
  createWasmOptionsManager,
  useCanvasCapture,
  useRenderLoop,
  useClientConnection,
  useWebcamStream,
} from "../hooks";
import { buildCapabilitiesPacket } from "../network";

export function ClientPage() {
  const rendererRef = useRef<AsciiRendererHandle>(null);
  const videoRef = useRef<HTMLVideoElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const lastFrameTimeRef = useRef<number>(0);
  const frameIntervalRef = useRef<number>(1000 / 60); // 60 FPS
  const frameCountRef = useRef<number>(0);
  const receivedFrameCountRef = useRef<number>(0);
  const frameReceiptTimesRef = useRef<number[]>([]);

  const [serverUrl, setServerUrl] = useState<string>(DISCOVERY_SERVICE_URL);
  const [showSettings, setShowSettings] = useState(false);
  const [terminalDimensions, setTerminalDimensions] = useState({
    cols: 0,
    rows: 0,
  });
  const [fps, setFps] = useState<number | undefined>();

  // Settings state (must be declared before hooks that use it)
  const [settings, setSettings] = useState<SettingsConfig>(DEFAULT_SETTINGS);

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

  // Use client connection hook
  const {
    clientRef,
    status,
    publicKey,
    connectionState,
    showModal,
    setShowModal,
    error,
    setError,
    wasmInitialized,
    connectToServer,
    handleDisconnect,
  } = useClientConnection({
    serverUrl,
    terminalDimensions,
    settings,
    rendererRef,
    frameQueueRef,
    uniqueReceivedFramesRef,
    frameCountRef,
    receivedFrameCountRef,
    frameReceiptTimesRef,
    onWasmInitialized: () => {
      // WASM initialized callback
    },
  });

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
      mapColorModeToClient,
      mapColorFilterToClient,
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
        "[Client] No testServerUrl in query, using default server URL",
      );
    }
  }, []);

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

  // Use webcam stream hook
  const { startWebcam, stopWebcam, isWebcamRunning } = useWebcamStream({
    clientRef,
    connectionState,
    settings,
    captureFrame,
    canvasRef,
    videoRef,
    frameIntervalRef,
    lastFrameTimeRef,
    frameQueueRef,
    setError,
  });

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
    [connectionState, optionsManager, settings, clientRef],
  );

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

  const renderFrame = useCallback(
    (deltaMs: number) => {
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

      // Delta-time frame queue draining: drop stale frames on slow hardware
      if (frameQueueRef.current.length > 0 && rendererRef.current) {
        if (renderLoopStartTimeRef.current === 0) {
          renderLoopStartTimeRef.current = performance.now();
        }

        const MAX_DRAIN = 4;
        const framesToDrain = Math.min(
          Math.floor(deltaMs / frameIntervalRef.current),
          MAX_DRAIN,
        );

        // Drop stale frames (skip all but the newest)
        for (let i = 0; i < framesToDrain - 1; i++) {
          frameQueueRef.current.shift();
        }

        // Render the newest frame
        const frameContent = frameQueueRef.current.shift();
        if (frameContent) {
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
      }
    },
    [frameIntervalRef],
  );

  const { startRenderLoop } = useRenderLoop(
    renderFrame,
    frameIntervalRef,
    lastFrameTimeRef,
  );

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
    // Note: stopWebcam is NOT in deps array to avoid circular dependency issues
    // oxlint-disable-next-line react-hooks/exhaustive-deps
  }, []);

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
    // oxlint-disable-next-line react-hooks/exhaustive-deps
  }, [connectionState]);

  useEffect(() => {
    if (connectionState === ConnectionState.CONNECTED && !isWebcamRunning) {
      console.log("[Client] Connected and ready, auto-starting webcam...");
      void startWebcam();
    }
  }, [connectionState, isWebcamRunning, startWebcam]);

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
      <AsciiChatWebHead
        title="Client - ascii-chat Web Client"
        description="Connect to an ascii-chat server. Real-time encrypted video chat rendered as ASCII art in your browser."
        url={`${SITES.WEB}/client`}
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
            connectionState={connectionState}
            wasmModule={getClientModule()}
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
