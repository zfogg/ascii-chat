/**
 * High-level client connection manager
 * Orchestrates WebSocket, WASM crypto, and packet handling
 */

import { SocketBridge } from './SocketBridge';
import {
  initClientWasm,
  cleanupClientWasm,
  generateKeypair,
  setServerPublicKey,
  performHandshake,
  encryptPacket,
  decryptPacket,
  parsePacket,
  serializePacket,
  getConnectionState,
  ConnectionState,
  PacketType,
  type ParsedPacket
} from '../wasm/client';

export interface ClientConnectionOptions {
  serverUrl: string;
  width?: number;
  height?: number;
}

export type ConnectionStateChangeCallback = (state: ConnectionState) => void;
export type PacketReceivedCallback = (packet: ParsedPacket, payload: Uint8Array) => void;

export class ClientConnection {
  private socket: SocketBridge | null = null;
  private clientPublicKey: string | null = null;
  private onStateChangeCallback: ConnectionStateChangeCallback | null = null;
  private onPacketCallback: PacketReceivedCallback | null = null;

  constructor(private options: ClientConnectionOptions) {}

  /**
   * Initialize WASM and connect to server
   */
  async connect(): Promise<void> {
    console.log('[ClientConnection] Initializing WASM client...');

    // Initialize WASM module
    await initClientWasm({
      width: this.options.width,
      height: this.options.height
    });

    // Generate client keypair
    console.log('[ClientConnection] Generating keypair...');
    this.clientPublicKey = await generateKeypair();
    console.log('[ClientConnection] Client public key:', this.clientPublicKey);

    // Create WebSocket connection
    console.log('[ClientConnection] Connecting to server:', this.options.serverUrl);
    this.socket = new SocketBridge({
      url: this.options.serverUrl,
      onPacket: this.handlePacket.bind(this),
      onError: (error) => {
        console.error('[ClientConnection] WebSocket error:', error);
        this.onStateChangeCallback?.(ConnectionState.ERROR);
      },
      onStateChange: (state) => {
        console.log('[ClientConnection] WebSocket state:', state);
        if (state === 'open') {
          this.onStateChangeCallback?.(ConnectionState.CONNECTING);
        } else if (state === 'closed') {
          this.onStateChangeCallback?.(ConnectionState.DISCONNECTED);
        }
      }
    });

    await this.socket.connect();

    // Wait for server to initiate handshake
    // Server will send CRYPTO_KEY_EXCHANGE_INIT first
    this.onStateChangeCallback?.(ConnectionState.HANDSHAKE);
  }

  /**
   * Send client's public key in response to server's KEY_EXCHANGE_INIT
   */
  private sendKeyExchangeResponse(): void {
    console.log('[ClientConnection] Sending CRYPTO_KEY_EXCHANGE_RESP with client pubkey');

    const clientPubKeyHex = this.clientPublicKey!;
    const clientPubKeyBytes = this.hexToBytes(clientPubKeyHex);

    this.sendPacket(PacketType.CRYPTO_KEY_EXCHANGE_RESP, clientPubKeyBytes);
  }

  /**
   * Handle server's key exchange init
   */
  private handleKeyExchangeInit(serverPubKeyBytes: Uint8Array): void {
    console.log('[ClientConnection] Received CRYPTO_KEY_EXCHANGE_INIT from server');

    // Convert to hex
    const serverPubKeyHex = Array.from(serverPubKeyBytes)
      .map(b => b.toString(16).padStart(2, '0'))
      .join('');

    console.log('[ClientConnection] Server public key:', serverPubKeyHex);

    // Set server's public key and compute shared secret
    setServerPublicKey(serverPubKeyHex);
    performHandshake();

    console.log('[ClientConnection] Shared secret computed');

    // Now send our public key in response
    this.sendKeyExchangeResponse();
  }

  /**
   * Convert hex string to bytes
   */
  private hexToBytes(hex: string): Uint8Array {
    const bytes = new Uint8Array(hex.length / 2);
    for (let i = 0; i < hex.length; i += 2) {
      bytes[i / 2] = parseInt(hex.substr(i, 2), 16);
    }
    return bytes;
  }

  /**
   * Handle incoming packet from WebSocket
   */
  private handlePacket(rawPacket: Uint8Array): void {
    try {
      console.log('[ClientConnection] Received packet, length:', rawPacket.length);

      // Parse packet header
      const parsed = parsePacket(rawPacket);
      console.log('[ClientConnection] Parsed packet type:', parsed.type);

      // Extract payload (skip header)
      const HEADER_SIZE = 20; // sizeof(packet_header_t)
      const payload = rawPacket.slice(HEADER_SIZE);

      // Handle handshake packets (unencrypted)
      if (parsed.type === PacketType.CRYPTO_KEY_EXCHANGE_INIT) {
        this.handleKeyExchangeInit(payload);
        return;
      }

      if (parsed.type === PacketType.CRYPTO_HANDSHAKE_COMPLETE) {
        console.log('[ClientConnection] Server confirmed handshake complete');
        this.onStateChangeCallback?.(ConnectionState.CONNECTED);
        return;
      }

      // Decrypt if we're in connected state
      const state = getConnectionState();
      let decryptedPayload = payload;

      if (state === ConnectionState.CONNECTED) {
        try {
          decryptedPayload = new Uint8Array(decryptPacket(payload));
          console.log('[ClientConnection] Decrypted payload, length:', decryptedPayload.length);
        } catch (error) {
          console.error('[ClientConnection] Decryption failed:', error);
          return;
        }
      }

      // Call user callback with parsed metadata and payload
      this.onPacketCallback?.(parsed, decryptedPayload);
    } catch (error) {
      console.error('[ClientConnection] Failed to handle packet:', error);
    }
  }

  /**
   * Send a packet to server
   */
  sendPacket(packetType: number, payload: Uint8Array): void {
    if (!this.socket || !this.socket.isConnected()) {
      throw new Error('Not connected to server');
    }

    try {
      // Encrypt payload if we're in connected state
      const state = getConnectionState();
      let finalPayload = payload;

      if (state === ConnectionState.CONNECTED) {
        finalPayload = encryptPacket(payload);
        console.log('[ClientConnection] Encrypted payload, length:', finalPayload.length);
      }

      // Serialize packet with header
      const packet = serializePacket(packetType, finalPayload, 0 /* client_id TBD */);
      console.log('[ClientConnection] Sending packet, type:', packetType, 'length:', packet.length);

      // Send over WebSocket
      this.socket.send(packet);
    } catch (error) {
      console.error('[ClientConnection] Failed to send packet:', error);
      throw error;
    }
  }

  /**
   * Register callback for connection state changes
   */
  onStateChange(callback: ConnectionStateChangeCallback): void {
    this.onStateChangeCallback = callback;
  }

  /**
   * Register callback for received packets
   */
  onPacketReceived(callback: PacketReceivedCallback): void {
    this.onPacketCallback = callback;
  }

  /**
   * Get client's public key
   */
  getPublicKey(): string | null {
    return this.clientPublicKey;
  }

  /**
   * Get current connection state
   */
  getState(): ConnectionState {
    return getConnectionState();
  }

  /**
   * Disconnect and cleanup
   */
  disconnect(): void {
    console.log('[ClientConnection] Disconnecting...');
    if (this.socket) {
      this.socket.close();
      this.socket = null;
    }
    cleanupClientWasm();
    this.onStateChangeCallback?.(ConnectionState.DISCONNECTED);
  }
}
