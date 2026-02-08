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
  private reconnectAttempts = 0;
  private maxReconnectAttempts = 5;
  private reconnectDelay = 1000; // Start with 1 second

  constructor(private options: SocketBridgeOptions) {
    this.onPacketCallback = options.onPacket || null;
    this.onErrorCallback = options.onError || null;
    this.onStateChangeCallback = options.onStateChange || null;
  }

  /**
   * Connect to WebSocket server
   */
  async connect(): Promise<void> {
    return new Promise((resolve, reject) => {
      try {
        // Server expects "acip" WebSocket subprotocol
        console.log('[SocketBridge] Creating WebSocket connection to:', this.options.url);
        this.ws = new WebSocket(this.options.url, 'acip');
        this.ws.binaryType = 'arraybuffer';

        this.ws.onopen = () => {
          console.log('[SocketBridge] WebSocket connected');
          this.reconnectAttempts = 0;
          this.reconnectDelay = 1000;
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
          if (!event.wasClean) {
            console.error('[SocketBridge] ❌ Unclean close - will attempt reconnect');
          } else {
            console.log('[SocketBridge] ✅ Clean close');
          }
          this.onStateChangeCallback?.('closed');

          // Attempt reconnection if not a clean close
          if (!event.wasClean && this.reconnectAttempts < this.maxReconnectAttempts) {
            this.attemptReconnect();
          }
        };

        this.onStateChangeCallback?.('connecting');
      } catch (error) {
        reject(error);
      }
    });
  }

  /**
   * Attempt to reconnect with exponential backoff
   */
  private attemptReconnect(): void {
    this.reconnectAttempts++;
    console.log(`[SocketBridge] Reconnecting (attempt ${this.reconnectAttempts}/${this.maxReconnectAttempts})...`);

    setTimeout(() => {
      this.connect().catch((error) => {
        console.error('[SocketBridge] Reconnection failed:', error);
        this.onErrorCallback?.(error);
      });
    }, this.reconnectDelay);

    // Exponential backoff
    this.reconnectDelay = Math.min(this.reconnectDelay * 2, 30000); // Max 30 seconds
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
   * Close WebSocket connection
   */
  close(): void {
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
