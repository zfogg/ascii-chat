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
  packetTypeName,
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
      // Try to parse the packet to get its type name
      let typeInfo = `${rawPacket.length} bytes`;
      try {
        const parsed = parsePacket(rawPacket);
        typeInfo = `type=${parsed.type} (${packetTypeName(parsed.type)}) ${rawPacket.length} bytes`;
      } catch { /* ignore parse errors */ }
      console.error(`[ClientConnection] >>> WASM->JS->WS sending raw packet: ${typeInfo}`);
      if (!this.socket) {
        console.error('[ClientConnection] Cannot send packet - socket not connected');
        return;
      }
      this.socket.send(rawPacket);
      console.error(`[ClientConnection] >>> WASM->JS->WS packet sent OK`);
    });
    console.error('[ClientConnection] WASM send packet callback registered');

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
      const typeName = (type: number) => packetTypeName(type);

      // Parse packet header
      const parsed = parsePacket(rawPacket);
      const name = typeName(parsed.type);
      console.error(`[ClientConnection] <<< RECV packet type=${parsed.type} (${name}) len=${rawPacket.length} payload_len=${parsed.length} client_id=${parsed.client_id}`);

      // Extract payload (skip header)
      const HEADER_SIZE = 22; // sizeof(packet_header_t): magic(8) + type(2) + length(4) + crc32(4) + client_id(4)
      const payload = rawPacket.slice(HEADER_SIZE);

      // Handle handshake packets using WASM callbacks
      if (parsed.type === PacketType.CRYPTO_KEY_EXCHANGE_INIT) {
        console.error(`[ClientConnection] >>> Dispatching ${name} to WASM handleKeyExchangeInit (raw ${rawPacket.length} bytes)`);
        handleKeyExchangeInit(rawPacket);
        console.error(`[ClientConnection] <<< WASM handleKeyExchangeInit returned OK`);
        this.onStateChangeCallback?.(ConnectionState.HANDSHAKE);
        return;
      }

      if (parsed.type === PacketType.CRYPTO_AUTH_CHALLENGE) {
        console.error(`[ClientConnection] >>> Dispatching ${name} to WASM handleAuthChallenge (raw ${rawPacket.length} bytes)`);
        handleAuthChallenge(rawPacket);
        console.error(`[ClientConnection] <<< WASM handleAuthChallenge returned OK`);
        return;
      }

      if (parsed.type === PacketType.CRYPTO_HANDSHAKE_COMPLETE) {
        console.error(`[ClientConnection] >>> Dispatching ${name} to WASM handleHandshakeComplete (raw ${rawPacket.length} bytes)`);
        handleHandshakeComplete(rawPacket);
        console.error(`[ClientConnection] <<< WASM handleHandshakeComplete returned OK - transitioning to CONNECTED`);
        this.onStateChangeCallback?.(ConnectionState.CONNECTED);
        return;
      }

      // For non-handshake packets during handshake, log a warning
      const state = getConnectionState();
      if (state !== ConnectionState.CONNECTED) {
        console.error(`[ClientConnection] *** Got non-handshake packet ${name} (${parsed.type}) while in state ${ConnectionState[state]} (${state}) - ignoring`);
      }

      // Decrypt if we're in connected state
      let decryptedPayload = payload;

      if (state === ConnectionState.CONNECTED) {
        try {
          decryptedPayload = new Uint8Array(decryptPacket(payload));
          console.error(`[ClientConnection] Decrypted payload, length: ${decryptedPayload.length}`);
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

    const name = packetTypeName(packetType);
    try {
      // Encrypt payload if we're in connected state
      const state = getConnectionState();
      let finalPayload = payload;

      if (state === ConnectionState.CONNECTED) {
        finalPayload = encryptPacket(payload);
        console.error(`[ClientConnection] Encrypted payload for ${name}, length: ${finalPayload.length}`);
      }

      // Serialize packet with header
      const packet = serializePacket(packetType, finalPayload, 0 /* client_id TBD */);
      console.error(`[ClientConnection] >>> SEND packet type=${packetType} (${name}) total_len=${packet.length} payload_len=${payload.length}`);

      // Send over WebSocket
      this.socket.send(packet);
    } catch (error) {
      console.error(`[ClientConnection] Failed to send ${name} packet:`, error);
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
