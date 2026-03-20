import { useCallback, useEffect, useRef, useState } from "react";
import { ConnectionState, PacketType } from "../wasm/client";
import { ClientConnection } from "../network/ClientConnection";
import { parseAsciiFrame } from "../network/AsciiFrameParser";
import {
  buildCapabilitiesPacket,
  buildStreamStartPacket,
} from "../network/packetBuilders";
import type { AsciiRendererHandle } from "../components/AsciiRenderer";
import type { SettingsConfig } from "../components/Settings";

const STATE_NAMES: Record<number, string> = {
  [ConnectionState.DISCONNECTED]: "Disconnected",
  [ConnectionState.CONNECTING]: "Connecting",
  [ConnectionState.HANDSHAKE]: "Performing handshake",
  [ConnectionState.CONNECTED]: "Connected",
  [ConnectionState.ERROR]: "Error",
};

// Simple hash function for frame content
const hashFrame = (content: string): string => {
  let hash = 0;
  for (let i = 0; i < content.length; i += 10) {
    hash = (hash << 5) - hash + content.charCodeAt(i);
    hash = hash & hash; // Convert to 32bit integer
  }
  return hash.toString(36);
};

interface UseClientConnectionOptions {
  serverUrl: string;
  terminalDimensions: { cols: number; rows: number };
  settings: SettingsConfig;
  rendererRef: React.RefObject<AsciiRendererHandle | null>;
  frameQueueRef: React.MutableRefObject<string[]>;
  uniqueReceivedFramesRef: React.MutableRefObject<Record<string, number>>;
  frameCountRef: React.MutableRefObject<number>;
  receivedFrameCountRef: React.MutableRefObject<number>;
  frameReceiptTimesRef: React.MutableRefObject<number[]>;
  onWasmInitialized: () => void;
}

export function useClientConnection(options: UseClientConnectionOptions) {
  const {
    serverUrl,
    terminalDimensions,
    settings,
    rendererRef,
    frameQueueRef,
    uniqueReceivedFramesRef,
    frameCountRef,
    receivedFrameCountRef,
    frameReceiptTimesRef,
    onWasmInitialized,
  } = options;

  const clientRef = useRef<ClientConnection | null>(null);
  const [status, setStatus] = useState<string>("Connecting...");
  const [publicKey, setPublicKey] = useState<string>("");
  const [connectionState, setConnectionState] = useState<ConnectionState>(
    ConnectionState.DISCONNECTED,
  );
  const [showModal, setShowModal] = useState(false);
  const [error, setError] = useState<string>("");
  const [hasAutoConnected, setHasAutoConnected] = useState(false);
  const [wasmInitialized, setWasmInitialized] = useState(false);

  const reconnectAttemptRef = useRef<number>(0);
  const reconnectTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(
    null,
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
              uniqueRendered: Object.keys(uniqueReceivedFramesRef.current)
                .length,
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
        onWasmInitialized();

        clientRef.current = conn;
        const pubKey = conn.getPublicKey() || "";
        console.log(`[Client] Public key set: ${pubKey.substring(0, 20)}...`);
        setPublicKey(pubKey);
      } catch (err) {
        const errMsg = `${String(err)}`;
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
      serverUrl,
      terminalDimensions,
      settings,
      rendererRef,
      frameQueueRef,
      uniqueReceivedFramesRef,
      frameCountRef,
      receivedFrameCountRef,
      frameReceiptTimesRef,
      onWasmInitialized,
    ],
  );

  const handleDisconnect = useCallback(() => {
    console.log("[Client] handleDisconnect() called");

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

  // Auto-connect on mount with serverUrl
  // Continuously retry connection with exponential backoff until successful
  // This allows page load before server startup, silently retrying in background
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

  return {
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
  };
}
