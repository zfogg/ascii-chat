import { useAutoGridSize } from "../hooks/useAutoGridSize";
import { useUrlState } from "../hooks/useUrlState";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";

// Extend Window interface for frame metrics
declare global {
  interface Window {
    __clientFrameMetrics?: {
      rendered: number;
      received: number;
      changedReceived?: number;
      queueDepth: number;
      uniqueRendered?: number;
      frameHashes?: Record<string, number>;
      lastRenderedFrame?: string;
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
  PacketType,
  initClientWasm,
  getClientWasmModule,
} from "../wasm/client";
import {
  adoptMirrorWasmModule,
  resetAdoptedMirrorWasmModule,
  renderAudioVisualization,
  submitAudioVisualizationSamples,
} from "@ascii-chat/shared/wasm";
import type { MirrorModule } from "@ascii-chat/shared/wasm";
import {
  AsciiRenderer,
  BinarySettings,
  AsciiChatWebHead,
  PageLayout,
  ModeHeader,
} from "../components";
import type { AsciiRendererHandle, BinarySettingsConfig } from "../components";
import { HelpLabel } from "../components/HelpLabel";
import { Tooltip } from "../components/Tooltip";
import { LOCKED_SETTINGS_HELP } from "../components/lockedSettings";
import type { AsciiFrame } from "../network/AsciiFrameParser";
import { AsciiChatMode, DEFAULT_SETTINGS } from "../utils";
import { DISCOVERY_SERVICE_URL, SITES } from "@ascii-chat/shared/utils";
import { isTestMode } from "@ascii-chat/shared";
import {
  applyMirrorWasmSettings,
  useCanvasCapture,
  useRenderLoop,
  useClientConnection,
  useWebcamStream,
} from "../hooks";
import { buildCapabilitiesPacket } from "../network";
import { buildStreamStartPacket } from "../network";
import { AudioPipeline } from "../audio";
import { getConnectionSecurityLines } from "../network/connectionSecurity";
import { SecuritySetupModal } from "../components/SecuritySetupModal";
import {
  getActivePassword,
  getActivePrivateKey,
  getActiveVerificationKey,
  loadCryptoSettings,
  saveCryptoSettings,
  writeCryptoUrl,
} from "../utils/cryptoSettings";
import type {
  CryptoSettings,
  VerificationKeyTarget,
} from "../utils/cryptoSettings";
import type { ClientCryptoOptions } from "../wasm/client";
import {
  MEDIA_DEVICE_PREFERENCES_CHANGED,
  type MediaDevicePreferencesChange,
} from "../utils/mediaDevicePreferences";
import type { DiscoveryOptions } from "../network/WebRTCSession";

export function ClientPage({
  discoveryMode = false,
}: {
  discoveryMode?: boolean;
}) {
  const params = new URLSearchParams(window.location.search);
  const syntheticAudio = params.has("test");
  const [connectionRequested, setConnectionRequested] = useUrlState(
    "connect",
    false,
  );
  const requestedConnection =
    (connectionRequested || (!discoveryMode && isTestMode())) &&
    (!discoveryMode || !!params.get("session")?.trim());
  const [sessionName, setSessionName] = useUrlState("session", "");
  const [sessionPassword, setSessionPassword] = useUrlState(
    "sessionPassword",
    "",
    true,
  );
  const [connectionRoute, setConnectionRoute] = useUrlState<"all" | "relay">(
    "connectionRoute",
    "all",
  );
  const [turnUsername, setTurnUsername] = useUrlState("turnUsername", "");
  const [turnCredential, setTurnCredential] = useUrlState(
    "turnCredential",
    "",
    true,
  );
  const [signalingUrl, setSignalingUrl] = useUrlState(
    "signalingUrl",
    DISCOVERY_SERVICE_URL,
  );
  const [cryptoSettings, setCryptoSettings] =
    useState<CryptoSettings>(loadCryptoSettings);
  useEffect(() => {
    writeCryptoUrl(cryptoSettings);
  }, [cryptoSettings]);
  const [securityModalOpen, setSecurityModalOpen] = useUrlState(
    "crypto",
    false,
  );
  let signalingUsesWss = false;
  try {
    signalingUsesWss = new URL(signalingUrl).protocol === "wss:";
  } catch {
    // The URL field will show validation through the normal connection error.
  }
  const discoveryCryptoOptions = useMemo<ClientCryptoOptions>(() => {
    const options: ClientCryptoOptions = {};
    if (cryptoSettings.authenticationEnabled) {
      const password = getActivePassword(cryptoSettings);
      const identity = getActivePrivateKey(cryptoSettings);
      if (password) options.password = password;
      if (identity) options.identityPrivateKeyText = identity;
    }
    const expected = cryptoSettings.skipServerVerification
      ? undefined
      : getActiveVerificationKey(cryptoSettings, "discovery-service");
    if (expected) options.expectedServerPublicKeyText = expected;
    return options;
  }, [cryptoSettings]);
  const discoveryHasAuthMaterial =
    !!discoveryCryptoOptions.password ||
    !!discoveryCryptoOptions.identityPrivateKeyText ||
    !!discoveryCryptoOptions.expectedServerPublicKeyText;
  const discoveryApplicationEncryption =
    (cryptoSettings.customEncryption ?? !signalingUsesWss) ||
    discoveryHasAuthMaterial;
  const [stunUrls, setStunUrls] = useUrlState(
    "stunUrls",
    "stun:stun.ascii-chat.com:3478,stun:stun.l.google.com:19302",
  );
  const [turnUrls, setTurnUrls] = useUrlState(
    "turnUrls",
    "turn:turn.ascii-chat.com:3478",
  );
  const [audioEnabled, setAudioEnabled] = useState(false);
  const [webcamDisabledByUser, setWebcamDisabledByUser] = useState(false);
  const [rendererReady, setRendererReady] = useState(false);
  const [rendererError, setRendererError] = useState("");
  const [rendererRequested, setRendererRequested] =
    useState(requestedConnection);
  const pendingConnectRef = useRef(requestedConnection);
  const discoveryJoinGenerationRef = useRef(0);
  useEffect(() => {
    // The discovery join form does not need the Emscripten renderer. Loading a
    // pthread runtime while the user is still editing connection details can
    // monopolize the browser main thread before a session even exists.
    if (!rendererRequested) return;
    let active = true;
    // The client build exports the same terminal-renderer functions used by
    // AsciiRenderer. Share that initialized pthread runtime with the renderer
    // instead of creating a second WASM module and worker pool on this page.
    const initializeRenderer = initClientWasm().then(() => {
      const module = getClientWasmModule();
      if (!module) throw new Error("Client WASM module did not initialize");
      adoptMirrorWasmModule(module as unknown as MirrorModule);
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
  const [connecting, setConnecting] = useState(requestedConnection);
  const settingsRef = useRef<BinarySettingsConfig>(DEFAULT_SETTINGS);
  const micEnabledRef = useRef(false);
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
            applicationEncryption: discoveryApplicationEncryption,
            cryptoOptions: discoveryCryptoOptions,
            iceTransportPolicy: connectionRoute,
            turnUsername,
            turnCredential,
            stunServers: stunUrls
              .split(",")
              .map((url) => url.trim())
              .filter(Boolean),
            turnServers: turnUrls
              .split(",")
              .map((url) => url.trim())
              .filter(Boolean),
          }
        : undefined,
    [
      discoveryMode,
      sessionName,
      sessionPassword,
      signalingUrl,
      discoveryApplicationEncryption,
      discoveryCryptoOptions,
      stunUrls,
      turnUrls,
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

  const [serverUrl, setServerUrl] = useUrlState<string>(
    "serverUrl",
    params.get("testServerUrl") || "ws://localhost:27226",
  );
  let usesWss = false;
  try {
    usesWss =
      new URL(discoveryMode ? signalingUrl : serverUrl).protocol === "wss:";
  } catch {
    // The URL field will show validation through the normal connection error.
  }
  const connectionCryptoTarget: VerificationKeyTarget = discoveryMode
    ? "discovery-service"
    : "client-server";
  const connectionCryptoOptions = useMemo<ClientCryptoOptions>(() => {
    const options: ClientCryptoOptions = {};
    if (cryptoSettings.authenticationEnabled) {
      const password = getActivePassword(cryptoSettings);
      const identity = getActivePrivateKey(cryptoSettings);
      if (password) options.password = password;
      if (identity) options.identityPrivateKeyText = identity;
    }
    const expected = cryptoSettings.skipServerVerification
      ? undefined
      : getActiveVerificationKey(cryptoSettings, connectionCryptoTarget);
    if (expected) options.expectedServerPublicKeyText = expected;
    return options;
  }, [cryptoSettings, connectionCryptoTarget]);
  const connectionHasAuthMaterial =
    !!connectionCryptoOptions.password ||
    !!connectionCryptoOptions.identityPrivateKeyText ||
    !!connectionCryptoOptions.expectedServerPublicKeyText;
  const applicationEncryption =
    (cryptoSettings.customEncryption ?? !usesWss) || connectionHasAuthMaterial;
  const connectionSecurityLines = getConnectionSecurityLines(
    discoveryMode ? signalingUrl : serverUrl,
    discoveryMode,
    applicationEncryption,
  );
  const [showSettings, setShowSettings] = useUrlState("settings", false);
  const [connectionSettingsOpen, setConnectionSettingsOpen] = useUrlState(
    "connectionSettings",
    false,
  );
  const [terminalDimensions, setTerminalDimensions] = useState({
    cols: 0,
    rows: 0,
  });
  const terminalDimensionsRef = useRef(terminalDimensions);
  useEffect(() => {
    terminalDimensionsRef.current = terminalDimensions;
  }, [terminalDimensions]);
  const [fps, setFps] = useState<number | undefined>(0);

  // Settings state (must be declared before hooks that use it)
  // Discovery shares the native server cadence and targets display refresh.
  const [settings, setSettings] = useUrlState<BinarySettingsConfig>(
    "render",
    DEFAULT_SETTINGS,
  );
  useAutoGridSize(setSettings);

  useEffect(() => {
    settingsRef.current = settings;
  }, [settings]);
  useEffect(() => {
    micEnabledRef.current = micEnabled;
  }, [micEnabled]);

  // Render loop for displaying received frames at target FPS (decoupled from network arrival rate)
  const frameQueueRef = useRef<AsciiFrame[]>([]);
  const latestFrameRef = useRef<AsciiFrame | null>(null);

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
    let hash = 2_166_136_261;
    for (let i = 0; i < content.length; i++) {
      hash = Math.imul(hash ^ content.charCodeAt(i), 16_777_619);
    }
    return (hash >>> 0).toString(36);
  };

  const diagnosticFrameCountRef = useRef(0);
  const cumulativeUniqueFramesRef = useRef(0);
  const uniqueReceivedFramesRef = useRef<Record<string, number>>({}); // Track unique frames at reception
  const uniqueReceivedFrameCountRef = useRef(0);
  const changedReceivedFrameCountRef = useRef(0);
  const uniqueReceivedFrameOrderRef = useRef<string[]>([]);

  // Use client connection hook
  const {
    clientRef,
    status,
    publicKey,
    connectionState,
    error,
    setError,
    connectToServer,
    handleDisconnect,
  } = useClientConnection({
    autoConnect: false,
    applicationEncryption,
    cryptoOptions: connectionCryptoOptions,
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
    changedReceivedFrameCountRef,
    uniqueReceivedFrameOrderRef,
    frameCountRef,
    receivedFrameCountRef,
    frameReceiptTimesRef,
    onWasmInitialized: () => {
      // WASM initialized callback
    },
  });

  useEffect(() => {
    const handleMediaDeviceChange = (event: Event) => {
      const change = (event as CustomEvent<MediaDevicePreferencesChange>)
        .detail;
      if (!change) return;

      if (
        change.changedKeys.includes("microphoneId") &&
        micEnabledRef.current
      ) {
        void audioRef.current
          ?.replaceMicrophone(change.preferences.microphoneId)
          .catch((error: unknown) => setError(String(error)));
      }
      if (change.changedKeys.includes("speakerId")) {
        void audioRef.current
          ?.setSpeakerDevice(change.preferences.speakerId)
          .catch((error: unknown) => setError(String(error)));
      }
    };

    window.addEventListener(
      MEDIA_DEVICE_PREFERENCES_CHANGED,
      handleMediaDeviceChange,
    );
    return () =>
      window.removeEventListener(
        MEDIA_DEVICE_PREFERENCES_CHANGED,
        handleMediaDeviceChange,
      );
  }, [setError]);

  useEffect(() => {
    if (connectionState === ConnectionState.CONNECTED)
      setConnectionRequested(true);
  }, [connectionState, setConnectionRequested]);

  // Wait until the renderer reports a settled size before connecting. Discovery
  // sends capabilities as soon as its DataChannel opens, and the direct client
  // also needs the final dimensions before protocol startup.
  useEffect(() => {
    if (!pendingConnectRef.current) return;

    if (rendererError) {
      pendingConnectRef.current = false;
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
    const joinGeneration = discoveryJoinGenerationRef;
    const timer = window.setTimeout(() => {
      if (
        !pendingConnectRef.current ||
        generation !== discoveryJoinGenerationRef.current
      )
        return;

      pendingConnectRef.current = false;
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
        joinGeneration.current++;
    };
  }, [
    connectToServer,
    connectionRequested,
    connecting,
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
      setFps(0);
      return;
    }

    setFps(0);
    const frameCount = () => renderedFrameCountRef.current;
    let previousFrameCount = frameCount();
    let previousTime = performance.now();
    const intervalId = window.setInterval(() => {
      const now = performance.now();
      const currentFrameCount = frameCount();
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

  // Client and Discovery render through the same mirror WASM module as
  // /mirror. Apply the settings as soon as that renderer is ready instead of
  // waiting for the unrelated protocol WASM module to initialize.
  useEffect(() => {
    if (!rendererReady) return;
    try {
      applyMirrorWasmSettings(settings);
    } catch (err) {
      console.error("Failed to apply renderer settings:", err);
    }
  }, [rendererReady, settings]);

  // Update frame interval when target FPS changes
  useEffect(() => {
    frameIntervalRef.current = 1000 / settings.targetFps;
  }, [settings.targetFps]);

  // Use shared canvas capture hook
  const { captureFrame } = useCanvasCapture(
    videoRef,
    canvasRef,
    settings.flipX ?? false,
  );

  // Use webcam stream hook
  const { startWebcam, stopWebcam, isWebcamRunning } = useWebcamStream({
    includeAudio: audioEnabled,
    terminalDimensions,
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
    setConnectionRequested(false);
    setConnecting(false);
    pendingConnectRef.current = false;
    discoveryJoinGenerationRef.current++;
    // Audio callbacks can survive briefly while AudioContext.close() drains.
    // Stop them from sending into a DataChannel that disconnect() just closed.
    audioStreamStartedRef.current = false;
    stopWebcam();
    closeAudio();
    handleDisconnect();
  }, [stopWebcam, closeAudio, handleDisconnect, setConnectionRequested]);
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
          onMicrophoneSamples: (samples) => {
            const current = settingsRef.current;
            if (
              current.animationEnabled &&
              (current.animation === "waveform" || current.animation === "fft")
            ) {
              submitAudioVisualizationSamples(samples, "microphone");
            }
          },
          onPlaybackSamples: (samples) => {
            const current = settingsRef.current;
            if (
              current.animationEnabled &&
              (current.animation === "waveform" || current.animation === "fft")
            ) {
              submitAudioVisualizationSamples(samples);
            }
          },
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
      micEnabledRef.current = false;
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
      micEnabledRef.current = true;
      setMicEnabled(true);
    } catch (error) {
      setError(String(error));
    }
  };

  const handleDimensionsChange = useCallback(
    (dims: { cols: number; rows: number }) => {
      frameQueueRef.current = [];
      setTerminalDimensions(dims);

      // If connected, send updated dimensions to server
      if (clientRef.current && connectionState === ConnectionState.CONNECTED) {
        try {
          const payload = buildCapabilitiesPacket(
            dims.cols,
            dims.rows,
            settings.targetFps,
            settings.colorMode,
            settings.colorFilter,
            settings.palette,
            settings.paletteChars,
            settings.matrixRain,
            clientRef.current.videoCodecCapabilities,
          );
          clientRef.current.sendPacket(PacketType.CLIENT_CAPABILITIES, payload);
        } catch (err) {
          console.error("[Client] Failed to send capabilities on resize:", err);
        }
      }
    },
    [connectionState, settings, clientRef],
  );

  // The native server renders each client's ASCII stream according to its
  // advertised capabilities. Re-send them when the live settings panel
  // changes so color, palette, filter, and FPS updates take effect without a
  // reconnect.
  useEffect(() => {
    if (
      connectionState !== ConnectionState.CONNECTED ||
      terminalDimensions.cols <= 0 ||
      terminalDimensions.rows <= 0 ||
      !clientRef.current
    )
      return;
    try {
      clientRef.current.sendPacket(
        PacketType.CLIENT_CAPABILITIES,
        buildCapabilitiesPacket(
          terminalDimensions.cols,
          terminalDimensions.rows,
          settings.targetFps,
          settings.colorMode,
          settings.colorFilter,
          settings.palette,
          settings.paletteChars,
          settings.matrixRain,
          clientRef.current.videoCodecCapabilities,
        ),
      );
    } catch (err) {
      console.error("[Client] Failed to apply live capabilities:", err);
    }
  }, [clientRef, connectionState, settings, terminalDimensions]);

  // Expose frame count for testing
  useEffect(() => {
    const metrics = {
      rendered: frameCountRef.current,
      received: receivedFrameCountRef.current,
      changedReceived: changedReceivedFrameCountRef.current,
      queueDepth: frameQueueRef.current.length,
      uniqueRendered: cumulativeUniqueFramesRef.current,
      frameHashes: uniqueReceivedFramesRef.current,
    };
    window.__clientFrameMetrics = metrics;
  });

  const renderFrame = useCallback(
    (_deltaMs: number) => {
      // Drain new network frames when they arrive, but keep presenting the most
      // recent complete frame at the selected display cadence. WebSocket frame
      // delivery can be bursty; tying canvas writes directly to packet arrival
      // makes the visible FPS unnecessarily inherit that jitter.
      if (frameQueueRef.current.length > 0) {
        latestFrameRef.current = frameQueueRef.current.pop() ?? null;
        frameQueueRef.current.length = 0;
      }
      const frame = latestFrameRef.current;
      if (rendererRef.current) {
        if (renderLoopStartTimeRef.current === 0) {
          renderLoopStartTimeRef.current = performance.now();
        }

        {
          const activeSettings = settingsRef.current;
          const visualAnimation =
            activeSettings.animationEnabled &&
            (activeSettings.animation === "waveform" ||
              activeSettings.animation === "fft");
          const dimensions = visualAnimation
            ? terminalDimensionsRef.current
            : frame
              ? { cols: frame.header.width, rows: frame.header.height }
              : null;
          if (!dimensions || dimensions.cols <= 0 || dimensions.rows <= 0) {
            return;
          }

          let frameContent = frame?.ansiString ?? "";
          if (visualAnimation) {
            const visualizationSource = micEnabledRef.current
              ? "microphone"
              : "media";
            if (syntheticAudio) {
              const samples = new Float32Array(1024);
              const now = performance.now();
              for (let index = 0; index < samples.length; index++) {
                samples[index] =
                  Math.sin((index / samples.length) * Math.PI * 16 + now / 70) *
                    0.68 +
                  Math.sin((index / samples.length) * Math.PI * 53 + now / 31) *
                    0.22;
              }
              submitAudioVisualizationSamples(samples, visualizationSource);
            }
            frameContent = renderAudioVisualization(
              dimensions.cols,
              dimensions.rows,
              visualizationSource,
              activeSettings.animation === "fft" ? "fft" : "waveform",
            );
          }
          if (!frameContent) return;
          const frameHash = hashFrame(frameContent);
          const writeStartedAt = performance.now();
          const drewFrame = rendererRef.current.writeFrame(frameContent, {
            cols: dimensions.cols,
            rows: dimensions.rows,
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
            metrics.received = receivedFrameCountRef.current;
            metrics.changedReceived = changedReceivedFrameCountRef.current;
            metrics.uniqueRendered = cumulativeUniqueFramesRef.current;
            metrics.queueDepth = frameQueueRef.current.length;
            metrics.lastRenderedFrame = frameContent;
          }

          // Log render rate every 60 rendered frames (using diagnostic counter)
          if (diagnosticFrameCountRef.current % 60 === 0) {
            renderLoopStartTimeRef.current = performance.now();
            diagnosticFrameCountRef.current = 0;
            frameHashesRef.current = {};
          }
        }
      }
    },
    [syntheticAudio],
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
      resetAdoptedMirrorWasmModule();
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
      setWebcamDisabledByUser(false);
      return;
    }
    if (!webcamDisabledByUser && !webcamAutoStartedRef.current) {
      webcamAutoStartedRef.current = true;
      console.log("[Client] Connected and ready, auto-starting webcam...");
      void startWebcam();
    }
  }, [connectionState, isWebcamRunning, startWebcam, webcamDisabledByUser]);

  const disableWebcam = useCallback(() => {
    setWebcamDisabledByUser(true);
    stopWebcam();
  }, [stopWebcam]);

  const enableWebcam = useCallback(() => {
    setWebcamDisabledByUser(false);
    void startWebcam();
  }, [startWebcam]);

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
  const connectionInProgress =
    connectionState === ConnectionState.CONNECTING ||
    connectionState === ConnectionState.HANDSHAKE;
  const disabledSettingsHelp = settingsDisabled
    ? LOCKED_SETTINGS_HELP
    : undefined;
  const audioControls =
    connectionState === ConnectionState.CONNECTED ? (
      <>
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
            Mic {Math.round(audioLevels.microphone * 100)}% Â· Playback{" "}
            {Math.round(audioLevels.playback * 100)}%
          </output>
        )}
      </>
    ) : undefined;

  return (
    <>
      <AsciiChatWebHead
        title={`${discoveryMode ? "Discovery" : "Client"} - ascii-chat Web Client`}
        description="Connect to an ascii-chat server. Real-time encrypted video chat rendered as ASCII art in your browser."
        url={`${SITES.WEB}/client`}
      />
      <PageLayout
        videoRef={videoRef}
        canvasRef={canvasRef}
        header={
          <ModeHeader
            showSettings={showSettings}
            settingsPanel={
              <BinarySettings
                config={settings}
                onChange={setSettings}
                mode={AsciiChatMode.CLIENT}
              />
            }
            connectionPanel={
              discoveryMode ? (
                <div className="flex flex-col">
                  <form
                    className={`flex flex-wrap gap-3 items-end ${settingsDisabled ? "settings-locked" : ""}`}
                    onSubmit={(event) => {
                      event.preventDefault();
                      setConnectionRequested(true);
                      pendingConnectRef.current = true;
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
                          onChange={(event) =>
                            setSignalingUrl(event.target.value)
                          }
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
                          onChange={(event) =>
                            setSessionName(event.target.value)
                          }
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
                      type="button"
                      disabled={settingsDisabled}
                      onClick={() => setSecurityModalOpen(true)}
                      className="border-0 bg-terminal-8 text-terminal-fg enabled:cursor-pointer enabled:hover:bg-terminal-7 rounded px-3 py-2 disabled:cursor-not-allowed disabled:opacity-50"
                    >
                      Crypto
                    </button>
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
                      <details
                        className="flex-shrink-0"
                        open={connectionSettingsOpen}
                        onToggle={(event) =>
                          setConnectionSettingsOpen(event.currentTarget.open)
                        }
                      >
                        <summary className="cursor-pointer">
                          Connection settings
                        </summary>
                        <div className="flex flex-col gap-2 mt-2">
                          <Tooltip
                            text={disabledSettingsHelp}
                            className="contents"
                          >
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
                          <Tooltip
                            text={disabledSettingsHelp}
                            className="contents"
                          >
                            <label>
                              <HelpLabel
                                label="STUN URLs"
                                text="Enter one or more comma-separated STUN URLs, each starting with stun: or stuns:. Example: stun:stun.example.com:3478."
                              />
                              <input
                                aria-label="STUN URLs"
                                disabled={settingsDisabled}
                                value={stunUrls}
                                onChange={(event) =>
                                  setStunUrls(event.target.value)
                                }
                                className="bg-terminal-bg border border-terminal-8 rounded px-2 py-1 w-full"
                              />
                            </label>
                          </Tooltip>
                          <Tooltip
                            text={disabledSettingsHelp}
                            className="contents"
                          >
                            <label>
                              <HelpLabel
                                label="TURN URLs"
                                text="Enter one or more comma-separated TURN URLs, each starting with turn: or turns:. TURN uses the manual credentials below, or credentials from the discovery service."
                              />
                              <input
                                aria-label="TURN URLs"
                                disabled={settingsDisabled}
                                value={turnUrls}
                                onChange={(event) =>
                                  setTurnUrls(event.target.value)
                                }
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
                </div>
              ) : (
                <div className="flex flex-col">
                  <form
                    className={`flex flex-wrap gap-3 items-end ${settingsDisabled ? "settings-locked" : ""}`}
                    onSubmit={(event) => {
                      event.preventDefault();
                      setConnectionRequested(true);
                      pendingConnectRef.current = true;
                      discoveryJoinGenerationRef.current++;
                      setRendererRequested(true);
                      setConnecting(true);
                    }}
                  >
                    <Tooltip text={disabledSettingsHelp} className="contents">
                      <label className="flex flex-col gap-1 w-80 max-w-full min-w-0">
                        Server WebSocket URL
                        <input
                          aria-label="Server WebSocket URL"
                          disabled={settingsDisabled}
                          value={serverUrl}
                          onChange={(event) => setServerUrl(event.target.value)}
                          placeholder="ws://localhost:27226"
                          className="bg-terminal-bg border border-terminal-8 rounded px-3 py-2 w-full font-mono"
                        />
                      </label>
                    </Tooltip>
                    <Tooltip
                      text="Uses the ascii-chat custom crypto handshake. This must match the server password."
                      className="contents"
                    >
                      <label className="flex flex-col gap-1 w-56 max-w-full min-w-0">
                        Crypto password
                        <input
                          aria-label="Crypto password"
                          type="password"
                          autoComplete="current-password"
                          minLength={8}
                          maxLength={255}
                          disabled={settingsDisabled}
                          value={cryptoSettings.password}
                          onChange={(event) => {
                            const next = {
                              ...cryptoSettings,
                              password: event.target.value,
                              authenticationEnabled: true,
                            };
                            saveCryptoSettings(next);
                            setCryptoSettings(next);
                          }}
                          placeholder="Server --password"
                          className="bg-terminal-bg border border-terminal-8 rounded px-3 py-2 w-full font-mono"
                        />
                      </label>
                    </Tooltip>
                    <button
                      type="button"
                      disabled={settingsDisabled}
                      onClick={() => setSecurityModalOpen(true)}
                      className="border-0 bg-terminal-8 text-terminal-fg enabled:cursor-pointer enabled:hover:bg-terminal-7 rounded px-3 py-2 disabled:cursor-not-allowed disabled:opacity-50"
                    >
                      Crypto
                    </button>
                    {connectionState === ConnectionState.CONNECTED ? (
                      <button
                        type="button"
                        onClick={disconnectMedia}
                        className="border border-red-700 bg-red-700 text-white cursor-pointer hover:bg-red-800 hover:border-red-800 rounded px-3 py-2"
                      >
                        Disconnect
                      </button>
                    ) : connectionInProgress ? (
                      <button
                        type="button"
                        onClick={disconnectMedia}
                        className="border border-red-700 bg-red-700 text-white cursor-pointer hover:bg-red-800 hover:border-red-800 rounded px-3 py-2"
                      >
                        Cancel
                      </button>
                    ) : (
                      <button
                        type="submit"
                        disabled={settingsDisabled}
                        className="border border-green-700 bg-green-700 text-white enabled:cursor-pointer enabled:hover:bg-green-800 enabled:hover:border-green-800 rounded px-3 py-2 disabled:cursor-not-allowed disabled:opacity-50"
                      >
                        Connect
                      </button>
                    )}
                    <div className="flex items-center gap-3 w-full min-w-0">
                      <details
                        className="flex-shrink-0"
                        open={connectionSettingsOpen}
                        onToggle={(event) =>
                          setConnectionSettingsOpen(event.currentTarget.open)
                        }
                      >
                        <summary className="cursor-pointer">
                          Connection settings
                        </summary>
                        <div className="mt-2 text-sm text-terminal-8 space-y-1">
                          {connectionSecurityLines.map(({ label, value }) => (
                            <p key={label}>
                              {label}: {value}
                            </p>
                          ))}
                          {publicKey && (
                            <p className="break-all">Client key: {publicKey}</p>
                          )}
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
                </div>
              )
            }
            controlBar={{
              title: discoveryMode ? "Discovery mode" : "Client mode",
              status,
              statusDotColor: getStatusDotColor(),
              dimensions: terminalDimensions,
              fps,
              targetFps: settings.targetFps,
              isWebcamRunning,
              onStartWebcam:
                connectionState === ConnectionState.CONNECTED
                  ? discoveryMode
                    ? enableWebcam
                    : startWebcam
                  : undefined,
              onStopWebcam: isWebcamRunning
                ? discoveryMode
                  ? disableWebcam
                  : stopWebcam
                : undefined,
              webcamActionLabels: discoveryMode
                ? { start: "Enable webcam", stop: "Disable webcam" }
                : undefined,
              showConnectionButton: false,
              onSettingsClick: () => setShowSettings((open) => !open),
              showSettingsButton: true,
              settingsOpen: showSettings,
              statusControls: audioControls,
            }}
          />
        }
        renderer={
          rendererRequested ? (
            <AsciiRenderer
              ref={rendererRef}
              columns={settings.width}
              rows={settings.height}
              onDimensionsChange={handleDimensionsChange}
              initializeOptions={false}
              {...(discoveryMode ? {} : { onFpsChange: setFps })}
              error={discoveryMode ? rendererError : error || rendererError}
              showFps={isWebcamRunning}
              connectionState={connectionState}
              wasmModuleReady={rendererReady}
            />
          ) : undefined
        }
      />
      <SecuritySetupModal
        open={securityModalOpen}
        settings={cryptoSettings}
        defaultEncryptionEnabled={!usesWss}
        onClose={() => {
          writeCryptoUrl(cryptoSettings);
          setSecurityModalOpen(false);
        }}
        onSave={(next) => {
          saveCryptoSettings(next);
          setCryptoSettings(next);
          setSecurityModalOpen(false);
        }}
      />
    </>
  );
}
