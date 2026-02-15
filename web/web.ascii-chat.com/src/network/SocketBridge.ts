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
  private reconnectTimeoutId: NodeJS.Timeout | null = null;
  private reconnectAttempts = 0;
  private isUserDisconnecting = false;
  private wasEverConnected = false;
  private readonly RECONNECT_DELAY = 1000; // 1 second fixed delay

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

    this.ws.onopen = () => {
      console.log('[SocketBridge] WebSocket connected');
      this.wasEverConnected = true;
      this.reconnectAttempts = 0;
      this.onStateChangeCallback?.('open');
      resolve();
    };

    this.ws.onmessage = (event) => {
      const packet = new Uint8Array(event.data);
      const pktType = quickParseType(packet);
      console.error(`[SocketBridge] <<< RECV ${packet.length} bytes, pkt_type=${pktType} (0x${pktType?.toString(16) ?? '??'})`);
      this.onPacketCallback?.(packet);
    };

    this.ws.onerror = (event) => {
      console.error('[SocketBridge] WebSocket error event:', event);
      console.error('[SocketBridge] WebSocket readyState:', this.ws?.readyState);
      console.error('[SocketBridge] WebSocket error details - code:', (event as any).code, 'reason:', (event as any).reason);
      const error = new Error('WebSocket error');
      this.onErrorCallback?.(error);
      reject(error);
    };

    this.ws.onclose = (event) => {
      console.error(`[SocketBridge] WebSocket CLOSED: code=${event.code} reason="${event.reason}" wasClean=${event.wasClean}`);
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

    this.onStateChangeCallback?.('connecting');
  }

  /**
   * Schedule a reconnection attempt
   */
  private scheduleReconnect(): void {
    if (this.reconnectTimeoutId !== null) {
      return; // Already scheduled
    }

    // Check if we should retry
    const isProduction = process.env.NODE_ENV === 'production';
    const maxAttempts = isProduction ? 5 : Infinity;

    if (this.reconnectAttempts >= maxAttempts && isProduction) {
      console.error(`[SocketBridge] Max reconnection attempts (${maxAttempts}) reached, giving up`);
      return;
    }

    this.reconnectAttempts++;
    const attemptStr = isProduction ? `${this.reconnectAttempts}/${maxAttempts}` : `${this.reconnectAttempts}`;
    console.log(`[SocketBridge] Scheduling reconnect attempt ${attemptStr} in ${this.RECONNECT_DELAY}ms`);

    this.reconnectTimeoutId = setTimeout(() => {
      this.reconnectTimeoutId = null;
      console.log('[SocketBridge] Attempting to reconnect...');
      try {
        this.createAndSetupWebSocket(() => {}, () => {});
      } catch (error) {
        console.error('[SocketBridge] Reconnect attempt failed:', error);
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
