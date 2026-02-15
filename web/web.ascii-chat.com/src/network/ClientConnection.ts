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
  private isUserDisconnecting = false;
  private wasEverConnected = false;
  private wasmReinitInProgress = false;
  private deferredPackets: Uint8Array[] = [];

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
        // Don't set ERROR state for transient connection errors
        // The reconnection logic will handle these - just log them
      },
      onStateChange: (state) => {
        console.log('[ClientConnection] WebSocket state:', state);
        if (state === 'open') {
          console.log('[ClientConnection] WebSocket opened, setting state to CONNECTING');
          // On reconnection, reinitialize WASM client if we've connected before
          if (this.wasEverConnected) {
            console.log('[ClientConnection] Reinitializing WASM for reconnection...');
            this.wasmReinitInProgress = true;
            this.deferredPackets = [];
            this.reinitializeWasmForReconnect().then(() => {
              this.wasmReinitInProgress = false;
              // Process any packets that arrived while WASM was reinitializing
              const deferred = this.deferredPackets;
              this.deferredPackets = [];
              deferred.forEach(packet => this.handlePacket(packet));
            }).catch(error => {
              console.error('[ClientConnection] WASM reinit failed:', error);
              this.wasmReinitInProgress = false;
              // WASM reinit failure will be retried on next reconnection attempt
              // Don't set ERROR state - let reconnection logic handle it
            });
          }
          this.wasEverConnected = false; // Reset for new handshake
          this.onStateChangeCallback?.(ConnectionState.CONNECTING);
        } else if (state === 'connecting') {
          // SocketBridge is attempting to reconnect
          console.log('[ClientConnection] SocketBridge reconnecting...');
          this.onStateChangeCallback?.(ConnectionState.CONNECTING);
        } else if (state === 'closed') {
          console.log('[ClientConnection] WebSocket closed');
          if (this.isUserDisconnecting) {
            console.log('[ClientConnection] User-initiated disconnect');
            this.onStateChangeCallback?.(ConnectionState.DISCONNECTED);
          } else if (this.wasEverConnected) {
            console.log('[ClientConnection] Unexpected disconnect, SocketBridge will attempt reconnect');
            // State changes will come from 'connecting' or 'open' events
          } else {
            console.log('[ClientConnection] Disconnected before ever connecting');
            this.onStateChangeCallback?.(ConnectionState.DISCONNECTED);
          }
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
   * Reinitialize WASM client for reconnection after disconnect
   */
  private async reinitializeWasmForReconnect(): Promise<void> {
    try {
      console.log('[ClientConnection] Starting WASM reinitialization...');
      cleanupClientWasm();

      await initClientWasm({
        width: this.options.width,
        height: this.options.height
      });
      console.log('[ClientConnection] WASM reinitialized');

      // Regenerate keypair for new handshake
      this.clientPublicKey = await generateKeypair();
      console.log('[ClientConnection] New keypair generated:', this.clientPublicKey);

      // Re-register send callback
      registerSendPacketCallback((rawPacket: Uint8Array) => {
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

      console.log('[ClientConnection] WASM reinitialization complete');
    } catch (error) {
      console.error('[ClientConnection] WASM reinitialization failed:', error);
      this.onStateChangeCallback?.(ConnectionState.ERROR);
    }
  }

  /**
   * Handle incoming packet from WebSocket
   */
  private handlePacket(rawPacket: Uint8Array): void {
    // Defer packet handling if WASM is being reinitialized
    if (this.wasmReinitInProgress) {
      console.log('[ClientConnection] WASM reinit in progress, deferring packet...');
      this.deferredPackets.push(rawPacket);
      return;
    }

    try {
      const typeName = (type: number) => packetTypeName(type);

      // Parse packet header
      const parsed = parsePacket(rawPacket);
      const name = typeName(parsed.type);
      console.log(`[ClientConnection] ========== PACKET RECEIVED ==========`);
      console.log(`[ClientConnection] Type: ${parsed.type} (${name})`);
      console.log(`[ClientConnection] Raw packet size: ${rawPacket.length} bytes`);
      console.log(`[ClientConnection] Payload size: ${parsed.length} bytes`);
      console.log(`[ClientConnection] Client ID: ${parsed.client_id}`);
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
        this.wasEverConnected = true; // Mark that we've successfully connected at least once
        this.onStateChangeCallback?.(ConnectionState.CONNECTED);
        return;
      }

      // For non-handshake packets during handshake, log a warning
      const state = getConnectionState();
      if (state !== ConnectionState.CONNECTED) {
        console.error(`[ClientConnection] *** Got non-handshake packet ${name} (${parsed.type}) while in state ${ConnectionState[state]} (${state}) - ignoring`);
        return;
      }

      // Handle PACKET_TYPE_ENCRYPTED: decrypt to get inner packet, then process
      if (parsed.type === PacketType.ENCRYPTED) {
        console.log(`[ClientConnection] ========== ENCRYPTED PACKET ==========`);
        console.log(`[ClientConnection] Encrypted payload size: ${payload.length} bytes`);
        try {
          // Decrypt the payload (ciphertext) to get the inner plaintext packet (header + payload)
          console.log(`[ClientConnection] Calling decryptPacket...`);
          const plaintext = new Uint8Array(decryptPacket(payload));
          console.log(`[ClientConnection] Decryption complete, plaintext length: ${plaintext.length} bytes`);
          console.error(`[ClientConnection] Decrypted ENCRYPTED packet, inner length: ${plaintext.length}`);

          // Parse the inner packet header
          const innerParsed = parsePacket(plaintext);
          const innerName = packetTypeName(innerParsed.type);
          console.log(`[ClientConnection] Inner packet type: ${innerParsed.type} (${innerName})`);
          console.log(`[ClientConnection] Inner packet payload size: ${innerParsed.length}`);
          console.error(`[ClientConnection] Inner packet: type=${innerParsed.type} (${innerName}) len=${innerParsed.length}`);

          // Extract inner payload (skip inner header)
          const innerPayload = plaintext.slice(HEADER_SIZE);
          console.log(`[ClientConnection] Inner payload extracted: ${innerPayload.length} bytes`);

          if (innerParsed.type === PacketType.ASCII_FRAME) {
            console.log(`[ClientConnection] ========== INNER PACKET IS ASCII_FRAME ==========`);
            console.log(`[ClientConnection] Calling user callback with ASCII_FRAME...`);
          }

          // Call user callback with the decrypted inner packet
          this.onPacketCallback?.(innerParsed, innerPayload);

          if (innerParsed.type === PacketType.ASCII_FRAME) {
            console.log(`[ClientConnection] ========== ASCII_FRAME CALLBACK COMPLETE ==========`);
          }
        } catch (error) {
          console.error('[ClientConnection] ========== DECRYPTION ERROR ==========');
          console.error('[ClientConnection] Failed to decrypt ENCRYPTED packet:', error);
          console.error('[ClientConnection] Error stack:', error instanceof Error ? error.stack : String(error));
          console.error('[ClientConnection] ========== END ERROR ==========');
        }
        return;
      }

      // Non-encrypted, non-handshake packet in connected state - pass through
      this.onPacketCallback?.(parsed, payload);
    } catch (error) {
      console.error('[ClientConnection] Failed to handle packet:', error);
    }
  }

  /**
   * Send a packet to server
   */
  sendPacket(packetType: number, payload: Uint8Array): void {
    if (!this.socket || !this.socket.isConnected()) {
      const name = packetTypeName(packetType);
      throw new Error(`Not connected to server (cannot send ${name} type=${packetType})`);
    }

    const name = packetTypeName(packetType);
    console.log(`[ClientConnection] sendPacket() called: type=${packetType} (${name}), payload_size=${payload.length}`);

    try {
      const state = getConnectionState();
      console.log(`[ClientConnection] Current connection state: ${state} (${ConnectionState[state]})`);

      if (state === ConnectionState.CONNECTED) {
        console.log(`[ClientConnection] State is CONNECTED, encrypting packet`);
        // Matching TCP transport protocol: encrypt entire packet, wrap in PACKET_TYPE_ENCRYPTED
        // 1. Build plaintext packet (header + payload)
        const plaintextPacket = serializePacket(packetType, payload, 0);
        console.log(`[ClientConnection] Built plaintext packet: ${plaintextPacket.length} bytes`);

        // 2. Encrypt the entire plaintext packet
        console.log(`[ClientConnection] Encrypting plaintext packet...`);
        const ciphertext = encryptPacket(plaintextPacket);
        console.log(`[ClientConnection] Encryption complete: ciphertext=${ciphertext.length} bytes`);

        // 3. Wrap ciphertext in PACKET_TYPE_ENCRYPTED header
        const encryptedPacket = serializePacket(PacketType.ENCRYPTED, ciphertext, 0);
        console.error(`[ClientConnection] >>> SEND encrypted ${name} (type=${packetType}) plaintext=${plaintextPacket.length} ciphertext=${ciphertext.length} wrapped=${encryptedPacket.length} bytes`);
        this.socket.send(encryptedPacket);
        console.log(`[ClientConnection] Encrypted ${name} packet sent to WebSocket`);
      } else if (state === ConnectionState.HANDSHAKE) {
        // During handshake, send unencrypted
        console.log(`[ClientConnection] State is HANDSHAKE, sending unencrypted`);
        const packet = serializePacket(packetType, payload, 0);
        console.error(`[ClientConnection] >>> SEND unencrypted packet type=${packetType} (${name}) total_len=${packet.length} payload_len=${payload.length}`);
        this.socket.send(packet);
        console.log(`[ClientConnection] Unencrypted ${name} packet sent to WebSocket`);
      } else {
        // In ERROR, DISCONNECTED, or CONNECTING state - don't send
        console.error(`[ClientConnection] Cannot send ${name} in state ${ConnectionState[state]} - packet dropped`);
      }
    } catch (error) {
      console.error(`[ClientConnection] Failed to send ${name} packet:`, error);
      console.error(`[ClientConnection] Error stack:`, error instanceof Error ? error.stack : String(error));
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
    this.isUserDisconnecting = true;

    if (this.socket) {
      this.socket.close();
      this.socket = null;
    }
    cleanupClientWasm();
    this.onStateChangeCallback?.(ConnectionState.DISCONNECTED);
  }
}
