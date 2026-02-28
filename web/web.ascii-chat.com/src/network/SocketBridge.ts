/**
 * WebSocket adapter for ascii-chat network layer
 * Bridges JavaScript WebSocket API to WASM client module
 */

/**
 * Parse packet type from raw bytes (first 10 bytes: 8 magic + 2 type).
 * Returns type in host byte order. Does NOT validate magic.
 */
function quickParseType(packet: Uint8Array): number | null {
  if (packet.length < 10) return null;
  // Type is at offset 8, 2 bytes, big-endian (network byte order)
  const b1 = packet[8];
  const b2 = packet[9];
  if (b1 !== undefined && b2 !== undefined) {
    return (b1 << 8) | b2;
  }
  return null;
}

export type PacketCallback = (packet: Uint8Array) => void;
export type ErrorCallback = (error: Error) => void;
export type StateCallback = (
  state: "connecting" | "open" | "closing" | "closed",
) => void;

export interface SocketBridgeOptions {
  url: string;
  onPacket?: PacketCallback;
  onError?: ErrorCallback;
  onStateChange?: StateCallback;
}

export class SocketBridge {
  private ws: WebSocket | null = null;
  private onPacketCallback: PacketCallback | null = null;
  private onErrorCallback: ErrorCallback | null = null;
  private onStateChangeCallback: StateCallback | null = null;
  private reconnectTimeoutId: ReturnType<typeof setTimeout> | null = null;
  private heartbeatTimeoutId: ReturnType<typeof setInterval> | null = null;
  private reconnectAttempts = 0;
  private isUserDisconnecting = false;
  private wasEverConnected = false;
  private readonly RECONNECT_DELAY = 1000; // 1 second fixed delay
  private readonly HEARTBEAT_INTERVAL = 1000; // 1 second heartbeat check (detect disconnects quickly)

  // ★ Fragment reassembly: WebSocket may fragment large packets
  private reassemblyBuffer: Uint8Array | null = null;
  private reassemblySize = 0;

  constructor(private options: SocketBridgeOptions) {
    this.onPacketCallback = options.onPacket || null;
    this.onErrorCallback = options.onError || null;
    this.onStateChangeCallback = options.onStateChange || null;
  }

  /**
   * Connect to WebSocket server with automatic reconnection
   */
  async connect(): Promise<void> {
    return new Promise((resolve, reject) => {
      try {
        this.createAndSetupWebSocket(resolve, reject);
      } catch (error) {
        reject(error);
      }
    });
  }

  /**
   * Internal method to create and setup WebSocket with event handlers
   */
  private createAndSetupWebSocket(
    resolve: () => void,
    reject: (error: Error) => void,
  ): void {
    console.log(
      "[SocketBridge] Creating WebSocket connection to:",
      this.options.url,
    );
    this.ws = new WebSocket(this.options.url, "acip");
    this.ws.binaryType = "arraybuffer";

    const handleOpen = () => {
      console.error("[SocketBridge] ✓✓✓ handleOpen CALLED");
      console.log("[SocketBridge] WebSocket connected");
      this.wasEverConnected = true;
      this.reconnectAttempts = 0;
      console.error("[SocketBridge] About to call onStateChangeCallback(open)");
      this.onStateChangeCallback?.("open");
      console.error(
        "[SocketBridge] onStateChangeCallback(open) returned, calling resolve()",
      );
      resolve();
      console.error("[SocketBridge] resolve() returned");
    };

    const handleMessage = (event: Event) => {
      const msgEvent = event as MessageEvent;
      const fragment = new Uint8Array(msgEvent.data);

      // ★ CRITICAL FIX: Reassemble WebSocket-fragmented messages
      // WebSocket may split large packets into multiple frames. We need to reassemble
      // them before passing to the packet handler.

      // Add fragment to reassembly buffer
      if (this.reassemblyBuffer === null) {
        // Start new reassembly with this fragment
        this.reassemblyBuffer = new Uint8Array(fragment);
        this.reassemblySize = fragment.length;
        console.error(
          `[SocketBridge] ★ START REASSEMBLY: received first fragment ${fragment.length} bytes`,
        );
      } else {
        // Append to existing reassembly buffer
        const newSize = this.reassemblySize + fragment.length;
        const newBuffer = new Uint8Array(newSize);
        newBuffer.set(this.reassemblyBuffer, 0);
        newBuffer.set(fragment, this.reassemblySize);
        this.reassemblyBuffer = newBuffer;
        this.reassemblySize = newSize;
        console.error(
          `[SocketBridge] ★ CONTINUE REASSEMBLY: appended ${fragment.length} bytes, total now ${newSize} bytes`,
        );
      }

      // ★ CRITICAL: Process ALL complete packets in the reassembly buffer
      // Don't just process one per handleMessage - multiple packets may be queued
      let packetsProcessed = 0;
      while (this.reassemblySize >= 14 && this.reassemblyBuffer !== null) {
        // Check if we have a complete ACIP packet by reading the length field
        // ACIP header: magic(8) + type(2) + length(4) + crc32(4) + client_id(4) = 22 bytes
        const data = this.reassemblyBuffer;
        const len =
          ((data[10] ?? 0) << 24) |
          ((data[11] ?? 0) << 16) |
          ((data[12] ?? 0) << 8) |
          (data[13] ?? 0);
        const expectedPacketSize = 22 + len; // Header + payload

        if (this.reassemblySize >= expectedPacketSize) {
          // We have a complete packet!
          const completePacket = this.reassemblyBuffer.slice(
            0,
            expectedPacketSize,
          );
          const pktType = quickParseType(completePacket);
          const typeHex = pktType !== null ? pktType.toString(16) : "??";
          console.error(
            `[SocketBridge] ★ COMPLETE PACKET ${packetsProcessed + 1}: assembled from ${this.reassemblySize} bytes, extracted ${expectedPacketSize} bytes, pkt_type=${pktType} (0x${typeHex})`,
          );

          // Save any leftover data for next packet
          if (this.reassemblySize > expectedPacketSize) {
            const leftover = this.reassemblyBuffer.slice(expectedPacketSize);
            this.reassemblyBuffer = leftover;
            this.reassemblySize = leftover.length;
            console.error(
              `[SocketBridge] ★ LEFTOVER: saved ${leftover.length} bytes for next packet`,
            );
          } else {
            this.reassemblyBuffer = null;
            this.reassemblySize = 0;
          }

          // Pass complete packet to handler
          packetsProcessed++;
          this.onPacketCallback?.(completePacket);
        } else {
          // Not enough data for a complete packet yet
          console.error(
            `[SocketBridge] ★ INCOMPLETE: need ${expectedPacketSize} bytes, have ${this.reassemblySize} bytes, waiting for more fragments`,
          );
          break; // Exit loop and wait for next message
        }
      }

      if (packetsProcessed === 0 && this.reassemblySize < 14) {
        console.error(
          `[SocketBridge] ★ NEED MORE: only ${this.reassemblySize} bytes, need at least 14 to read length field`,
        );
      } else if (packetsProcessed > 0) {
        console.error(
          `[SocketBridge] ★ PROCESSED ${packetsProcessed} packet(s) from this message`,
        );
      }
    };

    const handleError = (event: Event) => {
      console.error("[SocketBridge] WebSocket error event:", event);
      console.error(
        "[SocketBridge] WebSocket readyState:",
        this.ws?.readyState,
      );
      const errorEvent = event as Event & { code?: string; reason?: string };
      console.error(
        "[SocketBridge] WebSocket error details - code:",
        errorEvent.code,
        "reason:",
        errorEvent.reason,
      );
      const error = new Error("WebSocket error");
      this.onErrorCallback?.(error);
      reject(error);
    };

    const handleClose = (event: Event) => {
      const closeEvent = event as CloseEvent;
      console.error(
        `[SocketBridge] WebSocket CLOSED: code=${closeEvent.code} reason="${closeEvent.reason}" wasClean=${closeEvent.wasClean}`,
      );
      this.stopHeartbeat();
      // ★ Clear reassembly buffer on disconnect
      this.reassemblyBuffer = null;
      this.reassemblySize = 0;
      this.onStateChangeCallback?.("closed");

      // If user explicitly closed the connection, don't reconnect
      if (this.isUserDisconnecting) {
        console.log(
          "[SocketBridge] User-initiated disconnect, not reconnecting",
        );
        return;
      }

      // Auto-reconnect if connection was dropped unexpectedly
      if (this.wasEverConnected) {
        console.log(
          "[SocketBridge] Unexpected disconnect, scheduling reconnect",
        );
        this.scheduleReconnect();
      }
    };

    // Use addEventListener for event handling (works better with mocks and real browsers)
    this.ws.addEventListener("open", handleOpen);
    this.ws.addEventListener("message", handleMessage);
    this.ws.addEventListener("error", handleError);
    this.ws.addEventListener("close", handleClose);

    this.onStateChangeCallback?.("connecting");

    // Handle race condition: onopen might fire before we set the handler
    // Check readyState after setting up handlers and manually fire if already open
    if (this.ws.readyState === WebSocket.OPEN) {
      console.log(
        "[SocketBridge] WebSocket already OPEN, firing handleOpen immediately",
      );
      handleOpen();
    }
  }

  /**
   * Schedule a reconnection attempt
   */
  private scheduleReconnect(): void {
    if (this.reconnectTimeoutId !== null) {
      return; // Already scheduled
    }

    // Always retry indefinitely - don't give up after N attempts
    this.reconnectAttempts++;
    console.log(
      `[SocketBridge] Scheduling reconnect attempt ${this.reconnectAttempts} in ${this.RECONNECT_DELAY}ms`,
    );

    this.reconnectTimeoutId = setTimeout(() => {
      this.reconnectTimeoutId = null;
      console.error(
        `[SocketBridge] ⏱️ RECONNECT TIMER FIRED: attempt ${this.reconnectAttempts}, calling createAndSetupWebSocket`,
      );
      try {
        const resolve = () => {
          console.error(
            "[SocketBridge] ✅ Reconnection successful - resolve callback fired",
          );
        };
        const reject = (error: Error) => {
          console.error(
            `[SocketBridge] ❌ Reconnection failed (attempt ${this.reconnectAttempts}): ${error.message}`,
          );
          this.scheduleReconnect();
        };
        console.error(
          `[SocketBridge] About to call createAndSetupWebSocket with resolve/reject`,
        );
        this.createAndSetupWebSocket(resolve, reject);
        console.error(`[SocketBridge] createAndSetupWebSocket returned`);
      } catch (error) {
        console.error("[SocketBridge] Exception in reconnect timeout:", error);
        this.scheduleReconnect();
      }
    }, this.RECONNECT_DELAY);
  }

  /**
   * Send a packet over WebSocket
   */
  send(packet: Uint8Array): void {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      console.error(
        "[SocketBridge] ERROR: Cannot send - WebSocket state:",
        this.ws?.readyState,
      );
      throw new Error("WebSocket not connected");
    }
    const pktType = quickParseType(packet);
    const typeHex = pktType !== null ? pktType.toString(16) : "??";
    console.error(
      `[SocketBridge] >>> SEND ${packet.length} bytes, pkt_type=${pktType} (0x${typeHex})`,
    );
    this.ws.send(packet);
  }

  /**
   * Set packet callback
   */
  onPacket(callback: PacketCallback): void {
    this.onPacketCallback = callback;
  }

  /**
   * Set error callback
   */
  onError(callback: ErrorCallback): void {
    this.onErrorCallback = callback;
  }

  /**
   * Set state change callback
   */
  onStateChange(callback: StateCallback): void {
    this.onStateChangeCallback = callback;
  }

  /**
   * Start heartbeat to detect dead connections
   */
  startHeartbeat(): void {
    this.stopHeartbeat();
    console.log(
      "[SocketBridge] Starting heartbeat (interval:",
      this.HEARTBEAT_INTERVAL,
      "ms)",
    );
    this.heartbeatTimeoutId = setInterval(() => {
      if (!this.ws) {
        console.log("[SocketBridge] Heartbeat: WebSocket is null, stopping");
        this.stopHeartbeat();
        return;
      }

      const readyState = this.ws.readyState;
      if (readyState !== WebSocket.OPEN) {
        console.log(
          "[SocketBridge] Heartbeat: WebSocket not in OPEN state:",
          readyState,
        );
        this.stopHeartbeat();
        return;
      }

      // Try to send a test message to detect dead connections
      try {
        console.log("[SocketBridge] Heartbeat: sending test ping");
        this.ws.send(new Uint8Array([0xff])); // Send a single byte as keep-alive
      } catch (error) {
        console.error("[SocketBridge] Heartbeat: send failed:", error);
        // Manually trigger close to simulate what the browser should do
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
          console.error(
            "[SocketBridge] Heartbeat: forcing close due to send failure",
          );
          this.ws.close(1006, "Heartbeat send failed");
        }
      }
    }, this.HEARTBEAT_INTERVAL);
  }

  /**
   * Stop heartbeat
   */
  private stopHeartbeat(): void {
    if (this.heartbeatTimeoutId !== null) {
      clearInterval(this.heartbeatTimeoutId);
      this.heartbeatTimeoutId = null;
    }
  }

  /**
   * Close WebSocket connection (user-initiated)
   */
  close(): void {
    console.log("[SocketBridge] User calling close()");
    this.isUserDisconnecting = true;

    // Cancel any pending reconnection attempt
    if (this.reconnectTimeoutId !== null) {
      clearTimeout(this.reconnectTimeoutId);
      this.reconnectTimeoutId = null;
    }

    // Stop heartbeat
    this.stopHeartbeat();

    // Reset reconnection state when user explicitly closes
    this.reconnectAttempts = 0;

    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
  }

  /**
   * Check if connected
   */
  isConnected(): boolean {
    return this.ws !== null && this.ws.readyState === WebSocket.OPEN;
  }
}
