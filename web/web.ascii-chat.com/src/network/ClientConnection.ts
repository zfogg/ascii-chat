/**
 * High-level client connection manager
 * Orchestrates WebSocket, WASM crypto, and packet handling
 */

import { SocketBridge } from './SocketBridge';
import {
  initClientWasm,
  cleanupClientWasm,
  generateKeypair,
  setServerAddress,
  handleKeyExchangeInit,
  handleAuthChallenge,
  handleHandshakeComplete,
  registerSendPacketCallback,
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
    console.log('[ClientConnection] WASM init complete');

    // Register callback so WASM can send raw packets back through WebSocket
    registerSendPacketCallback((rawPacket: Uint8Array) => {
      console.log('[ClientConnection] WASM sending raw packet, length:', rawPacket.length);
      if (!this.socket) {
        console.error('[ClientConnection] Cannot send packet - socket not connected');
        return;
      }
      this.socket.send(rawPacket);
    });
    console.log('[ClientConnection] Packet callback registered');

    // Generate client keypair
    console.log('[ClientConnection] Generating keypair...');
    this.clientPublicKey = await generateKeypair();
    console.log('[ClientConnection] Client public key:', this.clientPublicKey);

    // Set server address for known_hosts verification
    const url = new URL(this.options.serverUrl);
    const serverHost = url.hostname;
    const serverPort = parseInt(url.port) || 27226; // Default WebSocket port
    console.log('[ClientConnection] Setting server address:', serverHost, serverPort);
    setServerAddress(serverHost, serverPort);

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
          console.log('[ClientConnection] Setting state to CONNECTING');
          this.onStateChangeCallback?.(ConnectionState.CONNECTING);
        } else if (state === 'closed') {
          console.log('[ClientConnection] Setting state to DISCONNECTED');
          this.onStateChangeCallback?.(ConnectionState.DISCONNECTED);
        }
      }
    });

    console.log('[ClientConnection] Waiting for WebSocket to connect...');
    await this.socket.connect();
    console.log('[ClientConnection] WebSocket connected!');

    // Wait for server to initiate handshake
    // Server will send CRYPTO_KEY_EXCHANGE_INIT first
    console.log('[ClientConnection] Setting state to HANDSHAKE');
    this.onStateChangeCallback?.(ConnectionState.HANDSHAKE);
    console.log('[ClientConnection] Connect complete');
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

      // Handle handshake packets using WASM callbacks
      if (parsed.type === PacketType.CRYPTO_KEY_EXCHANGE_INIT) {
        console.log('[ClientConnection] Handling CRYPTO_KEY_EXCHANGE_INIT via WASM');
        handleKeyExchangeInit(rawPacket);
        this.onStateChangeCallback?.(ConnectionState.HANDSHAKE);
        return;
      }

      if (parsed.type === PacketType.CRYPTO_AUTH_CHALLENGE) {
        console.log('[ClientConnection] Handling CRYPTO_AUTH_CHALLENGE via WASM');
        handleAuthChallenge(rawPacket);
        return;
      }

      if (parsed.type === PacketType.CRYPTO_HANDSHAKE_COMPLETE) {
        console.log('[ClientConnection] Handling CRYPTO_HANDSHAKE_COMPLETE via WASM');
        handleHandshakeComplete(rawPacket);
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
