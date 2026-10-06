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
    __webrtcBridgeMetrics?: {
      chunks: number;
      bytes: number;
      pending: number;
      dataType: string;
      isArrayBuffer: boolean;
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
import { HelpLabel } from "../components/HelpLabel";
import { Tooltip } from "../components/Tooltip";
import { LOCKED_SETTINGS_HELP } from "../components/lockedSettings";
import type { AsciiFrame } from "../network/AsciiFrameParser";
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
  const [connectionRoute, setConnectionRoute] = useState<"all" | "relay">(
    "all",
  );
  const [turnUsername, setTurnUsername] = useState("");
  const [turnCredential, setTurnCredential] = useState("");
  const [signalingUrl, setSignalingUrl] = useState(
    params.get("signalingUrl") || DISCOVERY_SERVICE_URL,
  );
  const [iceUrls, setIceUrls] = useState(
    "stun:stun.ascii-chat.com:3478,stun:stun.l.google.com:19302,turn:turn.ascii-chat.com:3478",
  );
  const [audioEnabled, setAudioEnabled] = useState(false);
  const [rendererReady, setRendererReady] = useState(false);
  const [rendererError, setRendererError] = useState("");
  const [rendererRequested, setRendererRequested] = useState(!discoveryMode);
  const pendingDiscoveryJoinRef = useRef(false);
  const discoveryJoinGenerationRef = useRef(0);
  useEffect(() => {
    // The discovery join form does not need the Emscripten renderer. Loading a
    // pthread runtime while the user is still editing connection details can
    // monopolize the browser main thread before a session even exists.
    if (!rendererRequested) return;
    let active = true;
    const initializeRenderer = initMirrorWasm(MirrorModuleFactory, {
      locateFile: (path) => `/wasm/${path}`,
    });
    void initializeRenderer
      .then(() => {
        if (active) setRendererReady(true);
      })
      .catch((error) => {
        if (active) setRendererError(String(error));
      });
    return () => {
      active = false;
    };
  }, [rendererRequested]);
  const [micEnabled, setMicEnabled] = useState(false);
  const [connecting, setConnecting] = useState(false);
  const audioRef = useRef<AudioPipeline | null>(null);
  const audioStreamStartedRef = useRef(false);
  const onConnectionStateChange = useCallback((state: ConnectionState) => {
    if (state !== ConnectionState.CONNECTED) {
      audioStreamStartedRef.current = false;
      audioRef.current?.stopCapture();
    }
  }, []);
  const [audioLevels, setAudioLevels] = useState({
    microphone: 0,
    playback: 0,
    sent: 0,
    played: 0,
    playedSamples: 0,
    underruns: 0,
  });
  const discovery = useMemo<DiscoveryOptions | undefined>(
    () =>
      discoveryMode
        ? {
            sessionName: sessionName.trim(),
            password: sessionPassword,
            signalingUrl,
            iceTransportPolicy: connectionRoute,
            turnUsername,
            turnCredential,
            iceServers: iceUrls
              .split(",")
              .map((url) => url.trim())
              .filter(Boolean)
              .map((urls) => ({ urls })),
          }
        : undefined,
    [
      discoveryMode,
      sessionName,
      sessionPassword,
      signalingUrl,
      iceUrls,
      connectionRoute,
      turnUsername,
      turnCredential,
    ],
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
  // Discovery shares the native server cadence and targets display refresh.
  const [settings, setSettings] = useState<SettingsConfig>(DEFAULT_SETTINGS);

  // Render loop for displaying received frames at target FPS (decoupled from network arrival rate)
  const frameQueueRef = useRef<AsciiFrame[]>([]);

  const renderLoopStartTimeRef = useRef<number>(0);
  const renderedFrameCountRef = useRef(0);

  const frameHashesRef = useRef<Record<string, number>>({});
  const renderTimingRef = useRef({
    frames: 0,
    totalMs: 0,
    maxMs: 0,
    windowStart: 0,
    previousFrameAt: 0,
    maxFrameGapMs: 0,
  });

  // Simple hash function for frame content
  const hashFrame = (content: string): string => {
    let hash = 0;
    for (let i = 0; i < content.length; i += 10) {
      hash = (hash << 5) - hash + content.charCodeAt(i);
      hash = hash & hash; // Convert to 32bit integer
    }
    return hash.toString(36);
  };

  const diagnosticFrameCountRef = useRef(0);
  const cumulativeUniqueFramesRef = useRef(0);
  const uniqueReceivedFramesRef = useRef<Record<string, number>>({}); // Track unique frames at reception
  const uniqueReceivedFrameCountRef = useRef(0);
  const uniqueReceivedFrameOrderRef = useRef<string[]>([]);

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
    onConnectionStateChange,
    serverUrl,
    terminalDimensions,
    settings,
    rendererRef,
    frameQueueRef,
    uniqueReceivedFramesRef,
    uniqueReceivedFrameCountRef,
    uniqueReceivedFrameOrderRef,
    frameCountRef,
    receivedFrameCountRef,
    frameReceiptTimesRef,
    onWasmInitialized: () => {
      // WASM initialized callback
    },
  });

  // A discovery peer receives its initial capabilities as soon as the
  // DataChannel opens. Wait until the renderer has reported a settled size so
  // that setup sends one authoritative capability packet instead of racing its
  // creation and ResizeObserver updates against the protocol startup.
  useEffect(() => {
    if (!discoveryMode || !pendingDiscoveryJoinRef.current) return;

    if (rendererError) {
      pendingDiscoveryJoinRef.current = false;
      setError(rendererError);
      setConnecting(false);
      return;
    }
    if (
      !rendererReady ||
      terminalDimensions.cols <= 0 ||
      terminalDimensions.rows <= 0
    )
      return;

    const generation = discoveryJoinGenerationRef.current;
    const settledDimensions = { ...terminalDimensions };
    const timer = window.setTimeout(() => {
      if (
        !pendingDiscoveryJoinRef.current ||
        generation !== discoveryJoinGenerationRef.current
      )
        return;

      pendingDiscoveryJoinRef.current = false;
      void connectToServer()
        .catch(() => {})
        .finally(() => setConnecting(false));
    }, 350);

    return () => {
      window.clearTimeout(timer);
      if (
        terminalDimensions.cols !== settledDimensions.cols ||
        terminalDimensions.rows !== settledDimensions.rows
      )
        discoveryJoinGenerationRef.current++;
    };
  }, [
    connectToServer,
    discoveryMode,
    rendererError,
    rendererReady,
    setError,
    terminalDimensions,
  ]);

  // Count frames the renderer successfully consumes, not RAF callbacks or
  // packets that are still waiting in the receive queue. Static images can
  // still be rendered at full frame rate when adjacent pixels are identical.
  useEffect(() => {
    if (connectionState !== ConnectionState.CONNECTED) {
      setFps(undefined);
      return;
    }

    setFps(0);
    let previousFrameCount = renderedFrameCountRef.current;
    let previousTime = performance.now();
    const intervalId = window.setInterval(() => {
      const now = performance.now();
      const currentFrameCount = renderedFrameCountRef.current;
      const elapsedSeconds = (now - previousTime) / 1000;
      setFps(
        elapsedSeconds > 0
          ? Math.round(
              (currentFrameCount - previousFrameCount) / elapsedSeconds,
            )
          : 0,
      );
      previousFrameCount = currentFrameCount;
      previousTime = now;
    }, 1000);

    return () => window.clearInterval(intervalId);
  }, [connectionState]);

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
    pendingDiscoveryJoinRef.current = false;
    discoveryJoinGenerationRef.current++;
    // Audio callbacks can survive briefly while AudioContext.close() drains.
    // Stop them from sending into a DataChannel that disconnect() just closed.
    audioStreamStartedRef.current = false;
    stopWebcam();
    closeAudio();
    handleDisconnect();
  }, [stopWebcam, closeAudio, handleDisconnect]);
  useEffect(() => {
    if (
      connectionState === ConnectionState.DISCONNECTED ||
      connectionState === ConnectionState.ERROR
    ) {
      // AudioWorklet callbacks may already be queued when a discovery peer is
      // replaced. Gate them before stopping capture so they cannot send stale
      // Opus packets on the next DataChannel before its STREAM_START arrives.
      audioStreamStartedRef.current = false;
    }

    if (connectionState === ConnectionState.DISCONNECTED) {
      stopWebcam();
      closeAudio();
      setMicEnabled(false);
    } else if (connectionState === ConnectionState.ERROR) {
      stopWebcam();
      // Keep the user-activated AudioContext alive during automatic discovery
      // reconnects, but stop sending microphone packets until the new server
      // connection has accepted STREAM_START.
      // Otherwise AUDIO_OPUS_BATCH can overtake STREAM_START and the server
      // disconnects the peer for sending audio before enabling the stream.
      if (discovery) audioRef.current?.stopCapture();
      else closeAudio();
    }
  }, [connectionState, stopWebcam, closeAudio, discovery]);
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
          onLevels: setAudioLevels,
          onAudioData: (payload) => {
            if (!audioStreamStartedRef.current) return;
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
      const client = clientRef.current;
      if (!client) throw new Error("Connect before starting the audio stream");
      client.sendPacket(PacketType.STREAM_START, buildStreamStartPacket(true));
      audioStreamStartedRef.current = true;
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
      const client = clientRef.current;
      if (!client) throw new Error("Connect before resuming the audio stream");
      client.sendPacket(PacketType.STREAM_START, buildStreamStartPacket(true));
      audioStreamStartedRef.current = true;
      await audioRef.current.startCapture();
      setMicEnabled(true);
    } catch (error) {
      setError(String(error));
    }
  };

  const handleDimensionsChange = useCallback(
    (dims: { cols: number; rows: number }) => {
      frameQueueRef.current = [];
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
      received: uniqueReceivedFrameCountRef.current,
      queueDepth: frameQueueRef.current.length,
      uniqueRendered: cumulativeUniqueFramesRef.current,
      frameHashes: uniqueReceivedFramesRef.current,
    };
    window.__clientFrameMetrics = metrics;
  });

  const renderFrame = useCallback((_deltaMs: number) => {
    // Delta-time frame queue draining: drop stale frames on slow hardware
    if (frameQueueRef.current.length > 0 && rendererRef.current) {
      if (renderLoopStartTimeRef.current === 0) {
        renderLoopStartTimeRef.current = performance.now();
      }

      // Display the latest snapshot instead of replaying a backlog.
      const frame = frameQueueRef.current.pop();
      frameQueueRef.current.length = 0;
      if (frame) {
        const frameContent = frame.ansiString;
        const frameHash = hashFrame(frameContent);
        const writeStartedAt = performance.now();
        const drewFrame = rendererRef.current.writeFrame(frameContent, {
          cols: frame.header.width,
          rows: frame.header.height,
        });
        const writeDurationMs = performance.now() - writeStartedAt;
        if (!drewFrame) {
          return;
        }
        renderedFrameCountRef.current++;
        const timing = renderTimingRef.current;
        const renderedAt = performance.now();
        if (!timing.windowStart) timing.windowStart = renderedAt;
        if (timing.previousFrameAt) {
          timing.maxFrameGapMs = Math.max(
            timing.maxFrameGapMs,
            renderedAt - timing.previousFrameAt,
          );
        }
        timing.previousFrameAt = renderedAt;
        timing.frames++;
        timing.totalMs += writeDurationMs;
        timing.maxMs = Math.max(timing.maxMs, writeDurationMs);
        if (timing.frames >= 60) {
          console.info(
            "[ClientRenderTiming]",
            JSON.stringify({
              frames: timing.frames,
              elapsedMs: Math.round(renderedAt - timing.windowStart),
              avgWriteMs: Number((timing.totalMs / timing.frames).toFixed(2)),
              maxWriteMs: Number(timing.maxMs.toFixed(2)),
              maxFrameGapMs: Number(timing.maxFrameGapMs.toFixed(2)),
            }),
          );
          timing.frames = 0;
          timing.totalMs = 0;
          timing.maxMs = 0;
          timing.windowStart = renderedAt;
          timing.maxFrameGapMs = 0;
        }
        // Track if this is a new unique frame we haven't seen before
        if (!frameHashesRef.current[frameHash]) {
          cumulativeUniqueFramesRef.current++;
        }
        frameHashesRef.current[frameHash] =
          (frameHashesRef.current[frameHash] || 0) + 1;

        frameCountRef.current++;
        diagnosticFrameCountRef.current++;
        const metrics = window.__clientFrameMetrics;
        if (metrics) {
          metrics.rendered = frameCountRef.current;
          metrics.uniqueRendered = cumulativeUniqueFramesRef.current;
          metrics.queueDepth = frameQueueRef.current.length;
        }

        // Log render rate every 60 rendered frames (using diagnostic counter)
        if (diagnosticFrameCountRef.current % 60 === 0) {
          renderLoopStartTimeRef.current = performance.now();
          diagnosticFrameCountRef.current = 0;
          frameHashesRef.current = {};
        }
      }
    }
  }, []);

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
    if (
      !discovery ||
      !audioEnabled ||
      connectionState !== ConnectionState.CONNECTED
    )
      return;
    try {
      const client = clientRef.current;
      if (!client) return;
      client.sendPacket(PacketType.STREAM_START, buildStreamStartPacket(true));
      audioStreamStartedRef.current = true;
      if (micEnabled) {
        void audioRef.current?.startCapture().catch((error) => {
          console.error(
            "[Client] Could not resume microphone after reconnect:",
            error,
          );
        });
      }
    } catch (error) {
      console.warn(
        "[Client] Could not resume audio stream after reconnect:",
        error,
      );
    }
  }, [audioEnabled, connectionState, discovery, clientRef, micEnabled]);

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

  const settingsDisabled =
    connecting ||
    connectionState === ConnectionState.CONNECTING ||
    connectionState === ConnectionState.HANDSHAKE ||
    connectionState === ConnectionState.CONNECTED;
  const disabledSettingsHelp = settingsDisabled
    ? LOCKED_SETTINGS_HELP
    : undefined;

  return (
    <>
      <AsciiChatWebHead
        title={`${discoveryMode ? "Discovery" : "Client"} - ascii-chat Web Client`}
        description="Connect to an ascii-chat server. Real-time encrypted video chat rendered as ASCII art in your browser."
        url={`${SITES.WEB}/client`}
      />
      {!discoveryMode && connectionState === ConnectionState.CONNECTED && (
        <div className="flex gap-3 px-4 py-2">
          <button
            onClick={() => {
              if (audioEnabled) closeAudio();
              else void enableAudio();
            }}
            className="border border-terminal-8 rounded px-3 py-1 cursor-pointer"
          >
            {audioEnabled ? "Disable speakers" : "Enable speakers"}
          </button>
          <button
            onClick={() => void toggleMicrophone()}
            className="border border-terminal-8 rounded px-3 py-1 cursor-pointer"
          >
            {micEnabled ? "Mute microphone" : "Enable microphone"}
          </button>
          {audioEnabled && (
            <output
              data-testid="audio-levels"
              data-sent={audioLevels.sent}
              data-played={audioLevels.played}
              data-played-samples={audioLevels.playedSamples}
              data-underruns={audioLevels.underruns}
              className="self-center text-sm text-terminal-8"
            >
              Mic {Math.round(audioLevels.microphone * 100)}% · Playback{" "}
              {Math.round(audioLevels.playback * 100)}%
            </output>
          )}
        </div>
      )}
      <PageLayout
        videoRef={videoRef}
        canvasRef={canvasRef}
        showSettings={showSettings}
        settingsPanel={
          <Settings
            config={settings}
            disabled={settingsDisabled}
            onChange={setSettings}
            mode={AsciiChatMode.CLIENT}
          />
        }
        topPanel={
          discoveryMode && (
            <div className="flex flex-col">
              <form
                className={`flex flex-wrap gap-3 items-end ${settingsDisabled ? "settings-locked" : ""}`}
                onSubmit={(event) => {
                  event.preventDefault();
                  pendingDiscoveryJoinRef.current = true;
                  discoveryJoinGenerationRef.current++;
                  setRendererRequested(true);
                  setConnecting(true);
                }}
              >
                <Tooltip text={disabledSettingsHelp} className="contents">
                  <label className="flex flex-col gap-1 w-56 max-w-full min-w-0">
                    Discovery service URL
                    <input
                      aria-label="Discovery service URL"
                      disabled={settingsDisabled}
                      value={signalingUrl}
                      onChange={(event) => setSignalingUrl(event.target.value)}
                      className="bg-terminal-bg border border-terminal-8 rounded px-3 py-2 w-full"
                    />
                  </label>
                </Tooltip>
                <Tooltip text={disabledSettingsHelp} className="contents">
                  <label className="flex flex-col gap-1 w-56 max-w-full min-w-0">
                    Session name
                    <input
                      aria-label="Session name"
                      disabled={settingsDisabled}
                      required
                      maxLength={47}
                      value={sessionName}
                      onChange={(event) => setSessionName(event.target.value)}
                      className="bg-terminal-bg border border-terminal-8 rounded px-3 py-2 w-full"
                      placeholder="blue-mountain-tiger"
                    />
                  </label>
                </Tooltip>
                <Tooltip text={disabledSettingsHelp} className="contents">
                  <label className="flex flex-col gap-1 w-56 max-w-full min-w-0">
                    Session password
                    <input
                      aria-label="Session password"
                      disabled={settingsDisabled}
                      type="password"
                      value={sessionPassword}
                      onChange={(event) =>
                        setSessionPassword(event.target.value)
                      }
                      className="bg-terminal-bg border border-terminal-8 rounded px-3 py-2 w-full"
                      autoComplete="off"
                    />
                  </label>
                </Tooltip>
                <button
                  type="submit"
                  disabled={settingsDisabled}
                  className="border border-green-700 bg-green-700 text-white enabled:cursor-pointer enabled:hover:bg-green-800 enabled:hover:border-green-800 rounded px-3 py-2 disabled:cursor-not-allowed disabled:opacity-50"
                >
                  Join session
                </button>
                {(connecting ||
                  connectionState === ConnectionState.CONNECTED) && (
                  <button
                    type="button"
                    onClick={disconnectMedia}
                    className="border border-red-700 bg-red-700 text-white cursor-pointer hover:bg-red-800 hover:border-red-800 rounded px-3 py-2"
                  >
                    {connecting ? "Cancel" : "Disconnect"}
                  </button>
                )}
                <div className="flex items-center gap-3 w-full min-w-0">
                  <details className="flex-shrink-0">
                    <summary className="cursor-pointer">
                      Connection settings
                    </summary>
                    <div className="flex flex-col gap-2 mt-2">
                      <Tooltip text={disabledSettingsHelp} className="contents">
                        <label className="flex flex-col gap-1 w-56 max-w-full">
                          Connection route
                          <select
                            aria-label="Connection route"
                            value={connectionRoute}
                            disabled={settingsDisabled}
                            onChange={(event) =>
                              setConnectionRoute(
                                event.target.value as "all" | "relay",
                              )
                            }
                            className="bg-terminal-bg border border-terminal-8 rounded px-3 py-2"
                          >
                            <option value="all">Automatic</option>
                            <option value="relay">Relay only</option>
                          </select>
                        </label>
                      </Tooltip>
                      <Tooltip text={disabledSettingsHelp} className="contents">
                        <label>
                          <HelpLabel
                            label="STUN/TURN URLs (comma-separated)"
                            text={
                              'Start each entry with stun:, turn:, or turns:. Optionally add a port with :port, and separate multiple URLs with commas. Example: "stun:stun.example.com, stun:stun.example.com:3478, turn:turn.example.com:3478".'
                            }
                          />
                          <input
                            aria-label="STUN/TURN URLs"
                            disabled={settingsDisabled}
                            value={iceUrls}
                            onChange={(event) => setIceUrls(event.target.value)}
                            className="bg-terminal-bg border border-terminal-8 rounded px-2 py-1 w-full"
                          />
                        </label>
                      </Tooltip>
                      <fieldset disabled={settingsDisabled}>
                        <div className="flex flex-wrap gap-3 mt-2">
                          <Tooltip
                            text={disabledSettingsHelp}
                            className="contents"
                          >
                            <label className="flex flex-col gap-1 w-56 max-w-full min-w-0">
                              <HelpLabel
                                label="TURN username"
                                text="Leave both TURN fields blank to use the credentials provided by the discovery service."
                              />
                              <input
                                aria-label="TURN username"
                                disabled={settingsDisabled}
                                value={turnUsername}
                                maxLength={127}
                                required={!!turnCredential}
                                onChange={(event) =>
                                  setTurnUsername(event.target.value)
                                }
                                autoComplete="off"
                                className="bg-terminal-bg border border-terminal-8 rounded px-3 py-2 w-full"
                              />
                            </label>
                          </Tooltip>
                          <Tooltip
                            text={disabledSettingsHelp}
                            className="contents"
                          >
                            <label className="flex flex-col gap-1 w-56 max-w-full min-w-0">
                              <HelpLabel
                                label="TURN password"
                                text="Leave both TURN fields blank to use the credentials provided by the discovery service."
                              />
                              <input
                                aria-label="TURN password"
                                disabled={settingsDisabled}
                                type="password"
                                value={turnCredential}
                                maxLength={127}
                                required={!!turnUsername}
                                onChange={(event) =>
                                  setTurnCredential(event.target.value)
                                }
                                autoComplete="off"
                                className="bg-terminal-bg border border-terminal-8 rounded px-3 py-2 w-full"
                              />
                            </label>
                          </Tooltip>
                        </div>
                      </fieldset>
                    </div>
                  </details>
                </div>
                {error && (
                  <p
                    role="alert"
                    title={error}
                    className="w-full text-terminal-1"
                  >
                    {error}
                  </p>
                )}
              </form>
              {connectionState === ConnectionState.CONNECTED && (
                <div className="flex gap-3 mt-3">
                  <button
                    onClick={() => {
                      if (audioEnabled) closeAudio();
                      else void enableAudio();
                    }}
                    className="border border-terminal-8 rounded px-3 py-1 cursor-pointer"
                  >
                    {audioEnabled ? "Disable speakers" : "Enable speakers"}
                  </button>
                  <button
                    onClick={() => void toggleMicrophone()}
                    className="border border-terminal-8 rounded px-3 py-1 cursor-pointer"
                  >
                    {micEnabled ? "Mute microphone" : "Enable microphone"}
                  </button>
                  {audioEnabled && (
                    <output
                      data-testid="audio-levels"
                      data-sent={audioLevels.sent}
                      data-played={audioLevels.played}
                      data-played-samples={audioLevels.playedSamples}
                      data-underruns={audioLevels.underruns}
                      className="self-center text-sm text-terminal-8"
                    >
                      Mic {Math.round(audioLevels.microphone * 100)}% · Playback{" "}
                      {Math.round(audioLevels.playback * 100)}%
                    </output>
                  )}
                </div>
              )}
            </div>
          )
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
            compactVerticalSpacing={discoveryMode}
          />
        }
        renderer={
          rendererRequested ? (
          <AsciiRenderer
            ref={rendererRef}
            onDimensionsChange={handleDimensionsChange}
            onFpsChange={setFps}
            error={discoveryMode ? rendererError : error || rendererError}
            showFps={isWebcamRunning}
            connectionState={connectionState}
            wasmModuleReady={rendererReady}
          />
          ) : undefined
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
