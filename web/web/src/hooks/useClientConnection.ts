import { RenderSettingsControl, SETTINGS_ACK } from "../network/RenderSettings";
import type { RuntimeValues } from "@ascii-chat/shared/wasm";
import { useLayoutEffect } from "react";
import { BrowserHostSession } from "../network/BrowserHostSession";
import { useVideoEncodings } from "./useVideoEncodings";
import { useCallback, useEffect, useRef, useState } from "react";
import { ConnectionState, PacketType } from "../wasm/client";
import type { ClientCryptoOptions } from "../wasm/client";
import {
  ClientConnection,
  parseAsciiFrame,
  buildCapabilitiesPacket,
  buildStreamStartPacket,
} from "../network";
import type { AsciiRendererHandle, BinarySettingsConfig } from "../components";
import type { ClientSession } from "../network/Transport";
import type { AsciiFrame } from "../network/AsciiFrameParser";
import { WebRTCSession, type DiscoveryOptions } from "../network/WebRTCSession";

const STATE_NAMES: Record<number, string> = {
  [ConnectionState.DISCONNECTED]: "Disconnected",
  [ConnectionState.CONNECTING]: "Connecting",
  [ConnectionState.HANDSHAKE]: "Performing handshake",
  [ConnectionState.CONNECTED]: "Connected",
  [ConnectionState.ERROR]: "Error",
};

// Simple hash function for frame content
const hashFrame = (content: string): string => {
  let hash = 2_166_136_261;
  for (let i = 0; i < content.length; i++) {
    hash = Math.imul(hash ^ content.charCodeAt(i), 16_777_619);
  }
  return (hash >>> 0).toString(36);
};

interface UseClientConnectionOptions {
  autoConnect?: boolean;
  applicationEncryption?: boolean;
  cryptoOptions?: ClientCryptoOptions;
  discovery?: DiscoveryOptions;
  onAudioPacket?: (type: number, payload: Uint8Array) => void;
  onConnectionStateChange?: (state: ConnectionState) => void;
  serverUrl: string;
  terminalDimensions: { cols: number; rows: number };
  settings: BinarySettingsConfig;
  rendererRef: React.RefObject<AsciiRendererHandle | null>;
  frameQueueRef: React.MutableRefObject<AsciiFrame[]>;
  uniqueReceivedFramesRef: React.MutableRefObject<Record<string, number>>;
  uniqueReceivedFrameCountRef: React.MutableRefObject<number>;
  changedReceivedFrameCountRef: React.MutableRefObject<number>;
  uniqueReceivedFrameOrderRef: React.MutableRefObject<string[]>;
  frameCountRef: React.MutableRefObject<number>;
  receivedFrameCountRef: React.MutableRefObject<number>;
  frameReceiptTimesRef: React.MutableRefObject<number[]>;
  onFrameAvailableRef?: React.RefObject<(() => void) | null>;
  onWasmInitialized: () => void;
}

export function useClientConnection(options: UseClientConnectionOptions) {
  const {
    autoConnect = false,
    applicationEncryption,
    cryptoOptions,
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
    onFrameAvailableRef,
    onWasmInitialized,
    discovery,
    onAudioPacket,
    onConnectionStateChange,
  } = options;

  const { validate: validateEncoding } = useVideoEncodings({
    fps: settings.targetFps,
  });
  const clientRef = useRef<ClientSession | null>(null);
  const [status, setStatus] = useState<string>("Disconnected");
  const [publicKey, setPublicKey] = useState<string>("");
  const [connectionState, setConnectionState] = useState<ConnectionState>(
    ConnectionState.DISCONNECTED,
  );
  const [showModal, setShowModal] = useState(false);
  const [error, setError] = useState<string>("");
  const [wasmInitialized, setWasmInitialized] = useState(false);
  const [renderSettingsStatus, setRenderSettingsStatus] = useState("");
  const renderControl = useRef<RenderSettingsControl | null>(null);
  const renderTimeout = useRef<ReturnType<typeof setTimeout> | null>(null);
  const sendRenderSettings = useCallback((values: RuntimeValues) => {
    if (!renderControl.current) return false;
    renderControl.current.send(values);
    setRenderSettingsStatus("Applying rendering settings…");
    if (renderTimeout.current) clearTimeout(renderTimeout.current);
    renderTimeout.current = setTimeout(
      () =>
        setRenderSettingsStatus("The server did not confirm these settings."),
      5000,
    );
    return true;
  }, []);

  const reconnectAttemptRef = useRef<number>(0);
  const receivedFrameHashRef = useRef<string | null>(null);
  const reconnectTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(
    null,
  );
  const hasBeenConnectedRef = useRef(false);
  const autoReconnectEnabledRef = useRef(false);
  const autoConnectStartedRef = useRef(false);

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

        validateEncoding();
        setStatus("Connecting...");
        if (showErrors) {
          hasBeenConnectedRef.current = false;
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

        const conn: ClientSession = discovery
          ? new (discovery.createSession ? BrowserHostSession : WebRTCSession)(
              { ...discovery, onProgress: setStatus },
              width,
              height,
            )
          : new ClientConnection({
              serverUrl,
              ...(applicationEncryption === undefined
                ? {}
                : { applicationEncryption }),
              ...(cryptoOptions ? { cryptoOptions } : {}),
              width,
              height,
            });

        if (discovery)
          autoReconnectEnabledRef.current = !discovery.createSession;

        renderControl.current = new RenderSettingsControl(conn);
        setRenderSettingsStatus("");
        conn.onStateChange((state) => {
          if (state === ConnectionState.CONNECTING) {
            renderControl.current = new RenderSettingsControl(conn);
            if (renderTimeout.current) clearTimeout(renderTimeout.current);
            setRenderSettingsStatus("");
          }
          onConnectionStateChange?.(state);
          const stateName = STATE_NAMES[state] || "Unknown";
          console.log(`[Client] State change: ${state} (${stateName})`);

          setConnectionState(state);
          setStatus(stateName);

          if (state === ConnectionState.CONNECTED) {
            hasBeenConnectedRef.current = true;
            reconnectAttemptRef.current = 0;
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
              `[Client] Renderer ref available: ${rendererRef.current ? "yes" : "no"}`,
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
                  settings.colorMode,
                  settings.colorFilter,
                  settings.palette,
                  settings.paletteChars,
                  settings.matrixRain,
                  conn.videoCodecCapabilities,
                  settings.renderMode,
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
                setError(`Could not start media: ${String(err)}`);
              }
            };
            sendSetupPackets();
          }

          if (state === ConnectionState.ERROR) {
            console.error("[Client] Connection error state reached");
            if (
              discovery &&
              (!hasBeenConnectedRef.current || conn.canReconnect === false)
            ) {
              // Initial join failures are usually actionable (unknown/full
              // session, missing password, or unsupported session config).
              // Repeating them cannot recover and creates a visible retry loop.
              autoReconnectEnabledRef.current = false;
            }
            if (showErrors && (!discovery || !hasBeenConnectedRef.current)) {
              setError("Connection error");
              setShowModal(true);
            }
          }
        });

        conn.onPacketReceived((parsed, decryptedPayload) => {
          if (clientRef.current !== conn) return;
          if (
            parsed.type === PacketType.AUDIO_BATCH ||
            parsed.type === PacketType.AUDIO_OPUS_BATCH
          ) {
            onAudioPacket?.(parsed.type, decryptedPayload);
          }
          if (parsed.type === SETTINGS_ACK) {
            const result = renderControl.current?.receive(decryptedPayload);
            if (result) {
              if (renderTimeout.current) clearTimeout(renderTimeout.current);
              setRenderSettingsStatus(
                result.status
                  ? "The server rejected these rendering settings."
                  : `Rendering settings applied · ${result.values["fps"]} FPS`,
              );
            }
          }
          if (parsed.type === PacketType.ASCII_FRAME) {
            receivedFrameCountRef.current++;
            const now = performance.now();
            frameReceiptTimesRef.current.push(now);
            if (frameReceiptTimesRef.current.length > 10) {
              frameReceiptTimesRef.current.splice(
                0,
                frameReceiptTimesRef.current.length - 10,
              );
            }

            try {
              const frame = parseAsciiFrame(decryptedPayload);
              // Track unique frames at reception (for measuring actual frames from server)
              const frameHash = hashFrame(frame.ansiString);
              const previousFrameHash = receivedFrameHashRef.current;
              if (previousFrameHash !== frameHash) {
                changedReceivedFrameCountRef.current++;
                receivedFrameHashRef.current = frameHash;
              }
              if (!uniqueReceivedFramesRef.current[frameHash]) {
                uniqueReceivedFrameCountRef.current++;
                uniqueReceivedFrameOrderRef.current.push(frameHash);
                if (uniqueReceivedFrameOrderRef.current.length > 256) {
                  const expiredHash =
                    uniqueReceivedFrameOrderRef.current.shift();
                  if (expiredHash !== undefined)
                    delete uniqueReceivedFramesRef.current[expiredHash];
                }
                uniqueReceivedFramesRef.current[frameHash] = 0;
              }
              uniqueReceivedFramesRef.current[frameHash]++;

              // Queue frame for the render loop to process at target FPS
              // This prevents frame accumulation when tab is hidden
              frameQueueRef.current.push(frame);
              onFrameAvailableRef?.current?.();
              if (frameQueueRef.current.length > 3)
                frameQueueRef.current.splice(
                  0,
                  frameQueueRef.current.length - 3,
                );

              window.__clientFrameMetrics = {
                lastRenderedFrame:
                  window.__clientFrameMetrics?.lastRenderedFrame ?? "",
                rendered: frameCountRef.current,
                received: receivedFrameCountRef.current,
                changedReceived: changedReceivedFrameCountRef.current,
                queueDepth: frameQueueRef.current.length,
                uniqueRendered:
                  window.__clientFrameMetrics?.uniqueRendered ?? 0,
                frameHashes: uniqueReceivedFramesRef.current,
              };
            } catch (err) {
              console.error("[Client] Failed to parse ASCII frame:", err);
            }
          }
        });

        console.log("[Client] Calling conn.connect()...");
        clientRef.current = conn;
        await conn.connect();
        console.log("[Client] conn.connect() completed successfully");

        if (clientRef.current !== conn) return;
        // Only mark WASM as initialized AFTER connection and WASM init complete
        setWasmInitialized(true);
        onWasmInitialized();

        clientRef.current = conn;
        const pubKey = conn.getPublicKey() || "";
        console.log(`[Client] Public key set: ${pubKey.substring(0, 20)}...`);
        setPublicKey(pubKey);
      } catch (err) {
        const errMsg = err instanceof Error ? err.message : String(err);
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
    [
      validateEncoding,
      serverUrl,
      applicationEncryption,
      cryptoOptions,
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
      onFrameAvailableRef,
      onWasmInitialized,
      discovery,
      onAudioPacket,
      onConnectionStateChange,
    ],
  );
  const connectToServerRef = useRef(connectToServer);
  useLayoutEffect(() => {
    connectToServerRef.current = connectToServer;
  }, [connectToServer]);

  // Discovery sessions should recover automatically when a peer connection
  // fails. Keep user-initiated disconnects manual; only retry sessions that
  // reached CONNECTED at least once.
  useEffect(() => {
    if (
      !discovery ||
      connectionState !== ConnectionState.ERROR ||
      !hasBeenConnectedRef.current ||
      !autoReconnectEnabledRef.current
    )
      return;

    let cancelled = false;
    const attemptReconnect = () => {
      if (
        cancelled ||
        !hasBeenConnectedRef.current ||
        !autoReconnectEnabledRef.current
      )
        return;
      reconnectAttemptRef.current++;
      const delayMs = Math.min(5000, 500 * reconnectAttemptRef.current);
      setStatus(
        `Connection lost; reconnecting in ${(delayMs / 1000).toFixed(1)}s`,
      );
      reconnectTimeoutRef.current = setTimeout(() => {
        reconnectTimeoutRef.current = null;
        void connectToServerRef
          .current({ showErrors: false })
          .catch((error) => {
            console.warn("[Client] Discovery reconnect failed:", error);
            attemptReconnect();
          });
      }, delayMs);
    };
    attemptReconnect();

    return () => {
      cancelled = true;
      if (reconnectTimeoutRef.current) {
        clearTimeout(reconnectTimeoutRef.current);
        reconnectTimeoutRef.current = null;
      }
    };
  }, [connectionState, discovery]);

  // Frame gaps can come from paused media or background-tab throttling.
  // Transport state callbacks, not video cadence, determine connection health.

  const handleDisconnect = useCallback(() => {
    console.log("[Client] handleDisconnect() called");
    autoReconnectEnabledRef.current = false;
    hasBeenConnectedRef.current = false;
    if (reconnectTimeoutRef.current) clearTimeout(reconnectTimeoutRef.current);
    reconnectTimeoutRef.current = null;

    if (clientRef.current) {
      console.log("[Client] Disconnecting from server");
      clientRef.current.disconnect();
      clientRef.current = null;
    }

    console.log("[Client] Clearing connection state");
    setConnectionState(ConnectionState.DISCONNECTED);
    setStatus("Disconnected");
    setPublicKey("");
  }, []);

  // Connecting is an explicit user action. `?connect` remains available for
  // links and automated tests, but it makes one visible attempt rather than
  // repeatedly opening sockets while somebody is editing the URL.
  useEffect(() => {
    if (discovery || !autoConnect || autoConnectStartedRef.current) return;
    autoConnectStartedRef.current = true;
    void connectToServer().catch(() => {});
  }, [autoConnect, connectToServer, discovery]);

  useEffect(
    () => () => {
      if (renderTimeout.current) clearTimeout(renderTimeout.current);
      clientRef.current?.disconnect();
      clientRef.current = null;
    },
    [],
  );

  return {
    clientRef,
    renderSettingsStatus,
    sendRenderSettings,
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
  };
}
