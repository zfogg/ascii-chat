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
  return (packet[8] << 8) | packet[9];
}

export type PacketCallback = (packet: Uint8Array) => void;
export type ErrorCallback = (error: Error) => void;
export type StateCallback = (state: 'connecting' | 'open' | 'closing' | 'closed') => void;

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
  private createAndSetupWebSocket(resolve: () => void, reject: (error: Error) => void): void {
    console.log('[SocketBridge] Creating WebSocket connection to:', this.options.url);
    this.ws = new WebSocket(this.options.url, 'acip');
    this.ws.binaryType = 'arraybuffer';

    const handleOpen = () => {
      console.error('[SocketBridge] ✓✓✓ handleOpen CALLED');
      console.log('[SocketBridge] WebSocket connected');
      this.wasEverConnected = true;
      this.reconnectAttempts = 0;
      console.error('[SocketBridge] About to call onStateChangeCallback(open)');
      this.onStateChangeCallback?.('open');
      console.error('[SocketBridge] onStateChangeCallback(open) returned, calling resolve()');
      resolve();
      console.error('[SocketBridge] resolve() returned');
    };

    const handleMessage = (event: Event) => {
      const msgEvent = event as MessageEvent;
      const packet = new Uint8Array(msgEvent.data);
      const pktType = quickParseType(packet);
      console.error(`[SocketBridge] <<< RECV ${packet.length} bytes, pkt_type=${pktType} (0x${pktType?.toString(16) ?? '??'})`);
      this.onPacketCallback?.(packet);
    };

    const handleError = (event: Event) => {
      console.error('[SocketBridge] WebSocket error event:', event);
      console.error('[SocketBridge] WebSocket readyState:', this.ws?.readyState);
      const errorEvent = event as any;
      console.error('[SocketBridge] WebSocket error details - code:', errorEvent.code, 'reason:', errorEvent.reason);
      const error = new Error('WebSocket error');
      this.onErrorCallback?.(error);
      reject(error);
    };

    const handleClose = (event: Event) => {
      const closeEvent = event as CloseEvent;
      console.error(`[SocketBridge] WebSocket CLOSED: code=${closeEvent.code} reason="${closeEvent.reason}" wasClean=${closeEvent.wasClean}`);
      this.stopHeartbeat();
      this.onStateChangeCallback?.('closed');

      // If user explicitly closed the connection, don't reconnect
      if (this.isUserDisconnecting) {
        console.log('[SocketBridge] User-initiated disconnect, not reconnecting');
        return;
      }

      // Auto-reconnect if connection was dropped unexpectedly
      if (this.wasEverConnected) {
        console.log('[SocketBridge] Unexpected disconnect, scheduling reconnect');
        this.scheduleReconnect();
      }
    };

    // Use addEventListener for event handling (works better with mocks and real browsers)
    this.ws.addEventListener('open', handleOpen);
    this.ws.addEventListener('message', handleMessage);
    this.ws.addEventListener('error', handleError);
    this.ws.addEventListener('close', handleClose);

    this.onStateChangeCallback?.('connecting');

    // Handle race condition: onopen might fire before we set the handler
    // Check readyState after setting up handlers and manually fire if already open
    if (this.ws.readyState === WebSocket.OPEN) {
      console.log('[SocketBridge] WebSocket already OPEN, firing handleOpen immediately');
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
    console.log(`[SocketBridge] Scheduling reconnect attempt ${this.reconnectAttempts} in ${this.RECONNECT_DELAY}ms`);

    this.reconnectTimeoutId = setTimeout(() => {
      this.reconnectTimeoutId = null;
      console.error(`[SocketBridge] ⏱️ RECONNECT TIMER FIRED: attempt ${this.reconnectAttempts}, calling createAndSetupWebSocket`);
      try {
        const resolve = () => {
          console.error('[SocketBridge] ✅ Reconnection successful - resolve callback fired');
        };
        const reject = (error: Error) => {
          console.error(`[SocketBridge] ❌ Reconnection failed (attempt ${this.reconnectAttempts}): ${error.message}`);
          this.scheduleReconnect();
        };
        console.error(`[SocketBridge] About to call createAndSetupWebSocket with resolve/reject`);
        this.createAndSetupWebSocket(resolve, reject);
        console.error(`[SocketBridge] createAndSetupWebSocket returned`);
      } catch (error) {
        console.error('[SocketBridge] Exception in reconnect timeout:', error);
        this.scheduleReconnect();
      }
    }, this.RECONNECT_DELAY);
  }

  /**
   * Send a packet over WebSocket
   */
  send(packet: Uint8Array): void {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      console.error('[SocketBridge] ERROR: Cannot send - WebSocket state:', this.ws?.readyState);
      throw new Error('WebSocket not connected');
    }
    const pktType = quickParseType(packet);
    console.error(`[SocketBridge] >>> SEND ${packet.length} bytes, pkt_type=${pktType} (0x${pktType?.toString(16) ?? '??'})`);
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
    console.log('[SocketBridge] Starting heartbeat (interval:', this.HEARTBEAT_INTERVAL, 'ms)');
    this.heartbeatTimeoutId = setInterval(() => {
      if (!this.ws) {
        console.log('[SocketBridge] Heartbeat: WebSocket is null, stopping');
        this.stopHeartbeat();
        return;
      }

      const readyState = this.ws.readyState;
      if (readyState !== WebSocket.OPEN) {
        console.log('[SocketBridge] Heartbeat: WebSocket not in OPEN state:', readyState);
        this.stopHeartbeat();
        return;
      }

      // Try to send a test message to detect dead connections
      try {
        console.log('[SocketBridge] Heartbeat: sending test ping');
        this.ws.send(new Uint8Array([0xFF])); // Send a single byte as keep-alive
      } catch (error) {
        console.error('[SocketBridge] Heartbeat: send failed:', error);
        // Manually trigger close to simulate what the browser should do
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
          console.error('[SocketBridge] Heartbeat: forcing close due to send failure');
          this.ws.close(1006, 'Heartbeat send failed');
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
    console.log('[SocketBridge] User calling close()');
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
