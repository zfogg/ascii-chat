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
} from "../wasm/client";
import {
  getColorFilter,
  initMirrorWasm,
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
import { buildStreamStartPacket } from "../network";
import { AudioPipeline } from "../audio";
import type { DiscoveryOptions } from "../network/WebRTCSession";
// @ts-expect-error - Generated Emscripten factory has no types
import MirrorModuleFactory from "../wasm/dist/mirror.js";

export function ClientPage({
  discoveryMode = false,
}: {
  discoveryMode?: boolean;
}) {
  const params = new URLSearchParams(window.location.search);
  const [sessionName, setSessionName] = useState(params.get("session") || "");
  const [sessionPassword, setSessionPassword] = useState("");
  const [signalingUrl, setSignalingUrl] = useState(
    params.get("signalingUrl") || DISCOVERY_SERVICE_URL,
  );
  const [iceUrls, setIceUrls] = useState(
    "stun:stun.ascii-chat.com:3478,stun:stun.l.google.com:19302,turn:turn.ascii-chat.com:3478",
  );
  const [audioEnabled, setAudioEnabled] = useState(false);
  const [rendererReady, setRendererReady] = useState(false);
  const [rendererError, setRendererError] = useState("");
  useEffect(() => {
    let active = true;
    void initMirrorWasm(MirrorModuleFactory, {
      locateFile: (path) => `/wasm/${path}`,
    })
      .then(() => {
        if (active) setRendererReady(true);
      })
      .catch((error) => {
        if (active) setRendererError(String(error));
      });
    return () => {
      active = false;
    };
  }, []);
  const [micEnabled, setMicEnabled] = useState(false);
  const [connecting, setConnecting] = useState(false);
  const audioRef = useRef<AudioPipeline | null>(null);
  const discovery = useMemo<DiscoveryOptions | undefined>(
    () =>
      discoveryMode
        ? {
            sessionName: sessionName.trim(),
            password: sessionPassword,
            signalingUrl,
            iceServers: iceUrls
              .split(",")
              .map((url) => url.trim())
              .filter(Boolean)
              .map((urls) => ({ urls })),
          }
        : undefined,
    [discoveryMode, sessionName, sessionPassword, signalingUrl, iceUrls],
  );
  const onAudioPacket = useCallback((type: number, payload: Uint8Array) => {
    try {
      audioRef.current?.playPacket(type, payload);
    } catch (error) {
      console.error("Audio playback failed", error);
    }
  }, []);
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
    ...(discovery ? { discovery } : {}),
    onAudioPacket,
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
    includeAudio: audioEnabled,
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

  const closeAudio = useCallback(() => {
    audioRef.current?.close();
    audioRef.current = null;
    setAudioEnabled(false);
    setMicEnabled(false);
  }, []);
  const disconnectMedia = useCallback(() => {
    stopWebcam();
    closeAudio();
    handleDisconnect();
  }, [stopWebcam, closeAudio, handleDisconnect]);
  useEffect(() => {
    if (
      connectionState === ConnectionState.DISCONNECTED ||
      connectionState === ConnectionState.ERROR
    ) {
      stopWebcam();
      closeAudio();
    }
  }, [connectionState, stopWebcam, closeAudio]);
  useEffect(
    () => () => {
      audioRef.current?.close();
    },
    [],
  );
  const enableAudio = async () => {
    try {
      if (!audioRef.current)
        audioRef.current = new AudioPipeline({
          onAudioData: (payload) => {
            try {
              clientRef.current?.sendPacket(
                PacketType.AUDIO_OPUS_BATCH,
                payload,
              );
            } catch (error) {
              console.error("Audio send failed", error);
            }
          },
        });
      await audioRef.current.enablePlayback();
      clientRef.current?.sendPacket(
        PacketType.STREAM_START,
        buildStreamStartPacket(true),
      );
      setAudioEnabled(true);
      return true;
    } catch (error) {
      setError(String(error));
      return false;
    }
  };
  const toggleMicrophone = async () => {
    if (micEnabled) {
      audioRef.current?.stopCapture();
      setMicEnabled(false);
      return;
    }
    try {
      if (!(await enableAudio())) return;
      if (!audioRef.current) return;
      clientRef.current?.sendPacket(
        PacketType.STREAM_START,
        buildStreamStartPacket(true),
      );
      await audioRef.current.startCapture();
      setMicEnabled(true);
    } catch (error) {
      setError(String(error));
    }
  };

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

  const webcamAutoStartedRef = useRef(false);
  useEffect(() => {
    if (connectionState !== ConnectionState.CONNECTED) {
      webcamAutoStartedRef.current = false;
      return;
    }
    if (!webcamAutoStartedRef.current) {
      webcamAutoStartedRef.current = true;
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
        title={`${discoveryMode ? "Discovery" : "Client"} - ascii-chat Web Client`}
        description="Connect to an ascii-chat server. Real-time encrypted video chat rendered as ASCII art in your browser."
        url={`${SITES.WEB}/client`}
      />
      {discoveryMode && (
        <form
          className="border-b border-terminal-8 p-4 flex flex-wrap gap-3 items-end"
          onSubmit={(event) => {
            event.preventDefault();
            setConnecting(true);
            void connectToServer()
              .catch(() => {})
              .finally(() => setConnecting(false));
          }}
        >
          <label className="flex flex-col gap-1">
            Session name
            <input
              aria-label="Session name"
              required
              maxLength={47}
              value={sessionName}
              disabled={
                connecting || connectionState === ConnectionState.CONNECTED
              }
              onChange={(event) => setSessionName(event.target.value)}
              className="bg-terminal-bg border border-terminal-8 rounded px-3 py-2"
              placeholder="blue-mountain-tiger"
            />
          </label>
          <label className="flex flex-col gap-1">
            Session password
            <input
              aria-label="Session password"
              type="password"
              value={sessionPassword}
              onChange={(event) => setSessionPassword(event.target.value)}
              className="bg-terminal-bg border border-terminal-8 rounded px-3 py-2"
              autoComplete="off"
            />
          </label>
          <button
            type="submit"
            disabled={
              connecting || connectionState === ConnectionState.CONNECTED
            }
            className="border border-terminal-4 rounded px-3 py-2 disabled:opacity-50"
          >
            Join session
          </button>
          {(connecting || connectionState === ConnectionState.CONNECTED) && (
            <button
              type="button"
              onClick={disconnectMedia}
              className="border border-terminal-8 rounded px-3 py-2"
            >
              {connecting ? "Cancel" : "Disconnect"}
            </button>
          )}
          <details className="w-full">
            <summary>Connection settings</summary>
            <div className="flex flex-col gap-2 mt-2">
              <label>
                Discovery service URL{" "}
                <input
                  aria-label="Discovery service URL"
                  value={signalingUrl}
                  onChange={(event) => setSignalingUrl(event.target.value)}
                  className="bg-terminal-bg border border-terminal-8 rounded px-2 py-1 w-full"
                />
              </label>
              <label>
                STUN/TURN URLs (comma-separated){" "}
                <input
                  aria-label="STUN/TURN URLs"
                  value={iceUrls}
                  onChange={(event) => setIceUrls(event.target.value)}
                  className="bg-terminal-bg border border-terminal-8 rounded px-2 py-1 w-full"
                />
              </label>
            </div>
          </details>
          <p role="status" className="w-full text-sm">
            {status}
          </p>
          {error && (
            <p role="alert" className="w-full text-terminal-1">
              {error}
            </p>
          )}
        </form>
      )}
      {connectionState === ConnectionState.CONNECTED && (
        <div className="flex gap-3 px-4 py-2">
          <button
            onClick={() => {
              if (audioEnabled) closeAudio();
              else void enableAudio();
            }}
            className="border border-terminal-8 rounded px-3 py-1"
          >
            {audioEnabled ? "Disable audio" : "Enable audio"}
          </button>
          <button
            onClick={() => void toggleMicrophone()}
            className="border border-terminal-8 rounded px-3 py-1"
          >
            {micEnabled ? "Mute microphone" : "Enable microphone"}
          </button>
        </div>
      )}
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
            title={discoveryMode ? "Discovery" : "Client"}
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
            showConnectionButton={!discoveryMode}
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
            error={error || rendererError}
            showFps={isWebcamRunning}
            connectionState={connectionState}
            wasmModuleReady={rendererReady}
          />
        }
        modal={
          discoveryMode ? undefined : (
            <ConnectionPanelModal
              isOpen={showModal}
              onClose={() => setShowModal(false)}
              connectionState={connectionState}
              status={status}
              publicKey={publicKey}
              serverUrl={serverUrl}
              onServerUrlChange={setServerUrl}
              onConnect={connectToServer}
              onDisconnect={disconnectMedia}
              isConnected={connectionState === ConnectionState.CONNECTED}
            />
          )
        }
      />
    </>
  );
}
