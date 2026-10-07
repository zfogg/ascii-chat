/**
 * High-level client connection manager
 * Orchestrates WebSocket, WASM crypto, and packet handling
 */

import { SocketBridge } from "./SocketBridge";
import type { PacketTransport } from "./Transport";
import {
  initClientWasm,
  cleanupClientWasm,
  generateKeypair,
  setServerAddress,
  handleCryptoParameters,
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
  type ParsedPacket,
  type ClientInitOptions,
} from "../wasm/client";

export interface ClientConnectionOptions {
  serverUrl: string;
  applicationEncryption?: boolean;
  discoveryHandshake?: boolean;
  width?: number;
  height?: number;
  wasmOptions?: Omit<ClientInitOptions, "width" | "height">;
}

export type ConnectionStateChangeCallback = (state: ConnectionState) => void;
export type PacketReceivedCallback = (
  packet: ParsedPacket,
  payload: Uint8Array,
) => void;

export class ClientConnection {
  readonly transportType = "websocket" as const;
  private socket: (PacketTransport & { connect(): Promise<void> }) | null =
    null;
  private transportState = ConnectionState.DISCONNECTED;
  private get usesApplicationEncryption(): boolean {
    return (
      this.options.applicationEncryption ??
      new URL(this.options.serverUrl).protocol !== "wss:"
    );
  }
  private clientPublicKey: string | null = null;
  private onStateChangeCallback: ConnectionStateChangeCallback | null = null;
  private onPacketCallback: PacketReceivedCallback | null = null;
  private isUserDisconnecting = false;
  private wasEverConnected = false;
  private wasmReinitInProgress = false;
  private deferredPackets: Uint8Array[] = [];
  private socketHasEverOpened = false; // Track if this socket instance has ever opened
  private packetStats: Map<number, number> = new Map(); // Track packets by type

  constructor(private options: ClientConnectionOptions) {}

  /**
   * Initialize WASM and connect to server
   */
  async connect(): Promise<void> {
    console.log("[ClientConnection] Initializing WASM client...");

    // Initialize WASM module
    const initOptions: ClientInitOptions = { ...this.options.wasmOptions };
    if (this.options.width !== undefined)
      initOptions.width = this.options.width;
    if (this.options.height !== undefined)
      initOptions.height = this.options.height;
    await initClientWasm(initOptions);
    if (this.isUserDisconnecting) return;
    console.log("[ClientConnection] WASM init complete");

    // Register callback so WASM can send raw packets back through WebSocket
    registerSendPacketCallback((rawPacket: Uint8Array) => {
      // Try to parse the packet to get its type name
      let typeInfo = `${rawPacket.length} bytes`;
      try {
        const parsed = parsePacket(rawPacket);
        typeInfo = `type=${parsed.type} (${packetTypeName(parsed.type)}) ${rawPacket.length} bytes`;
      } catch {
        /* ignore parse errors */
      }
      console.error(
        `[ClientConnection] >>> WASM->JS->WS sending raw packet: ${typeInfo}`,
      );
      if (!this.socket) {
        console.error(
          "[ClientConnection] Cannot send packet - socket not connected",
        );
        return;
      }
      this.socket.send(rawPacket);
      console.error(`[ClientConnection] >>> WASM->JS->WS packet sent OK`);
    });
    console.error("[ClientConnection] WASM send packet callback registered");

    // Generate client keypair
    console.log("[ClientConnection] Generating keypair...");
    this.clientPublicKey = await generateKeypair();
    console.log("[ClientConnection] Client public key:", this.clientPublicKey);

    if (this.isUserDisconnecting) return;
    // Set server address for known_hosts verification
    const url = new URL(this.options.serverUrl);
    const serverHost = url.hostname;
    const serverPort =
      parseInt(url.port) || (url.protocol === "wss:" ? 443 : 27226);
    console.log(
      "[ClientConnection] Setting server address:",
      serverHost,
      serverPort,
    );
    setServerAddress(serverHost, serverPort);

    await this.connectSocket();
  }

  private async connectSocket(): Promise<void> {
    // Create WebSocket connection
    console.log(
      "[ClientConnection] Connecting to server:",
      this.options.serverUrl,
    );
    this.socket = new SocketBridge({
      url: this.options.serverUrl,
      onPacket: this.handlePacket.bind(this),
      onError: (error) => {
        console.error("[ClientConnection] WebSocket error:", error);
        // Don't set ERROR state for transient connection errors
        // The reconnection logic will handle these - just log them
      },
      onStateChange: (state) => {
        console.error(
          `[ClientConnection] *** onStateChange: state='${state}' wasEverConnected=${this.wasEverConnected}`,
        );
        console.log("[ClientConnection] WebSocket state:", state);
        if (state === "open") {
          void this.handleSocketOpen();
        } else if (state === "connecting") {
          // SocketBridge is attempting to reconnect
          console.log("[ClientConnection] SocketBridge reconnecting...");
          this.onStateChangeCallback?.(ConnectionState.CONNECTING);
        } else if (state === "closed") {
          this.transportState = ConnectionState.DISCONNECTED;
          console.log("[ClientConnection] WebSocket closed");
          if (this.isUserDisconnecting) {
            console.log("[ClientConnection] User-initiated disconnect");
            this.onStateChangeCallback?.(ConnectionState.DISCONNECTED);
          } else if (this.wasEverConnected) {
            console.log(
              "[ClientConnection] Unexpected disconnect, SocketBridge will attempt reconnect",
            );
            // State changes will come from 'connecting' or 'open' events
          } else {
            console.log(
              "[ClientConnection] Disconnected before ever connecting",
            );
            this.onStateChangeCallback?.(ConnectionState.DISCONNECTED);
          }
        }
      },
    });

    console.log("[ClientConnection] Waiting for WebSocket to connect...");
    await this.socket.connect();
    if (!this.usesApplicationEncryption) return;
    console.log("[ClientConnection] WebSocket connected!");

    // Wait for server to initiate handshake
    // Server will send CRYPTO_KEY_EXCHANGE_INIT first
    console.log("[ClientConnection] Setting state to HANDSHAKE");
    this.onStateChangeCallback?.(ConnectionState.HANDSHAKE);
    console.log("[ClientConnection] Connect complete");
  }

  private async handleSocketOpen(): Promise<void> {
    const isReconnection = this.socketHasEverOpened;
    this.socketHasEverOpened = true;

    if (isReconnection && this.usesApplicationEncryption) {
      // Pause packet handling before resetting WASM. On reconnect the server
      // must not receive PROTOCOL_VERSION until the new crypto context and its
      // packet callback are ready to process the handshake response.
      this.wasmReinitInProgress = true;
      this.deferredPackets = [];
      this.onStateChangeCallback?.(ConnectionState.CONNECTING);
      cleanupClientWasm();

      const url = new URL(this.options.serverUrl);
      const serverHost = url.hostname;
      const serverPort =
        parseInt(url.port) || (url.protocol === "wss:" ? 443 : 27226);
      const reinitOptions: ClientInitOptions = { ...this.options.wasmOptions };
      if (this.options.width !== undefined)
        reinitOptions.width = this.options.width;
      if (this.options.height !== undefined)
        reinitOptions.height = this.options.height;

      try {
        await initClientWasm(reinitOptions);
        this.clientPublicKey = await generateKeypair();
        setServerAddress(serverHost, serverPort);
        registerSendPacketCallback((rawPacket: Uint8Array) => {
          if (this.socket) this.socket.send(rawPacket);
        });

        const deferred = this.deferredPackets;
        this.deferredPackets = [];
        this.wasmReinitInProgress = false;
        deferred.forEach((packet) => this.handlePacket(packet));
      } catch (error) {
        this.wasmReinitInProgress = false;
        console.error("[ClientConnection] WASM reinit failed:", error);
        this.transportState = ConnectionState.ERROR;
        this.onStateChangeCallback?.(ConnectionState.ERROR);
        return;
      }
    }

    if (this.isUserDisconnecting || !this.socket?.isConnected()) return;

    if (this.options.discoveryHandshake) {
      const version = new Uint8Array(16);
      new DataView(version.buffer).setUint16(0, 1, false);
      version[4] = this.usesApplicationEncryption ? 1 : 0;
      this.socket.send(serializePacket(PacketType.PROTOCOL_VERSION, version, 0));
    }

    if (!this.usesApplicationEncryption) {
      this.transportState = ConnectionState.CONNECTED;
      this.wasEverConnected = true;
      this.onStateChangeCallback?.(ConnectionState.CONNECTED);
      return;
    }

    this.onStateChangeCallback?.(ConnectionState.CONNECTING);
  }

  /**
   * Handle incoming packet from WebSocket
   */
  private handlePacket(rawPacket: Uint8Array): void {
    // Defer packet handling if WASM is being reinitialized
    if (this.wasmReinitInProgress) {
      console.error(
        `[ClientConnection] WASM reinit in progress, deferring packet (${rawPacket.length} bytes)`,
      );
      this.deferredPackets.push(rawPacket);
      return;
    }

    try {
      // Parse packet header
      const parsed = parsePacket(rawPacket);

      // Track packet statistics
      const count = (this.packetStats.get(parsed.type) || 0) + 1;
      this.packetStats.set(parsed.type, count);

      // Log stats every 100 packets
      // if (
      //   Array.from(this.packetStats.values()).reduce((a, b) => a + b, 0) %
      //     100 ===
      //   0
      // ) {
      //   const stats: string[] = [];
      //   for (const [type, cnt] of this.packetStats.entries()) {
      //     stats.push(`${packetTypeName(type)}=${cnt}`);
      //   }
      //   console.error(
      //     `[ClientConnection] PACKET STATS (total packets): ${stats.join(", ")}`,
      //   );
      // }

      const name = packetTypeName(parsed.type);
      console.log(`[ClientConnection] ========== PACKET RECEIVED ==========`);
      console.log(`[ClientConnection] Type: ${parsed.type} (${name})`);
      console.log(
        `[ClientConnection] Raw packet size: ${rawPacket.length} bytes`,
      );
      console.log(`[ClientConnection] Payload size: ${parsed.length} bytes`);
      console.log(`[ClientConnection] Client ID: ${parsed.client_id}`);
      console.error(
        `[ClientConnection] <<< RECV packet type=${parsed.type} (${name}) len=${rawPacket.length} payload_len=${parsed.length} client_id=${parsed.client_id}`,
      );

      // Extract payload (skip header)
      const HEADER_SIZE = 22; // sizeof(packet_header_t): magic(8) + type(2) + length(4) + crc32(4) + client_id(4)
      const payload = rawPacket.slice(HEADER_SIZE);

      // Handle handshake packets using WASM callbacks
      if (parsed.type === PacketType.CRYPTO_PARAMETERS) {
        // Server sends crypto parameters first to negotiate key sizes
        console.log(
          "[ClientConnection] Processing CRYPTO_PARAMETERS from server",
        );
        handleCryptoParameters(rawPacket);
        return;
      }

      if (parsed.type === PacketType.CRYPTO_KEY_EXCHANGE_INIT) {
        // console.error(
        //   `[ClientConnection] >>> Dispatching ${name} to WASM handleKeyExchangeInit (raw ${rawPacket.length} bytes)`,
        // );
        handleKeyExchangeInit(rawPacket);
        // console.error(
        //   `[ClientConnection] <<< WASM handleKeyExchangeInit returned OK`,
        // );
        this.onStateChangeCallback?.(ConnectionState.HANDSHAKE);
        return;
      }

      if (parsed.type === PacketType.CRYPTO_AUTH_CHALLENGE) {
        // console.error(
        //   `[ClientConnection] >>> Dispatching ${name} to WASM handleAuthChallenge (raw ${rawPacket.length} bytes)`,
        // );
        handleAuthChallenge(rawPacket);
        // console.error(
        //   `[ClientConnection] <<< WASM handleAuthChallenge returned OK`,
        // );
        return;
      }

      if (parsed.type === PacketType.CRYPTO_HANDSHAKE_COMPLETE) {
        // console.error(
        //   `[ClientConnection] >>> Dispatching ${name} to WASM handleHandshakeComplete (raw ${rawPacket.length} bytes)`,
        // );
        handleHandshakeComplete(rawPacket);
        // console.error(
        //   `[ClientConnection] <<< WASM handleHandshakeComplete returned OK - transitioning to CONNECTED`,
        // );
        this.wasEverConnected = true; // Mark that we've successfully connected at least once
        // Note: Don't start heartbeat here - it interferes with ACIP protocol
        // The protocol exchange will complete and normal operation will resume
        this.onStateChangeCallback?.(ConnectionState.CONNECTED);
        return;
      }

      // Allow SERVER_STATE packets during any state (they arrive early in connection)
      if (parsed.type === PacketType.SERVER_STATE) {
        // console.error(
        //   `[ClientConnection] >>> Got SERVER_STATE packet during ${ConnectionState[getConnectionState()]} state - processing anyway`,
        // );
        // Dispatch to callback but don't require CONNECTED state
        const payload = rawPacket.slice(22); // Skip header
        this.onPacketCallback?.(parsed, payload);
        return;
      }

      // For non-handshake packets during handshake, log a warning
      const state = this.getState();
      if (state !== ConnectionState.CONNECTED) {
        // console.error(
        //   `[ClientConnection] *** Got non-handshake packet ${name} (${parsed.type}) while in state ${ConnectionState[state]} (${state}) - ignoring`,
        // );
        return;
      }

      // Handle PACKET_TYPE_ENCRYPTED: decrypt to get inner packet, then process
      if (parsed.type === PacketType.ENCRYPTED) {
        // console.log(
        //   `[ClientConnection] ========== ENCRYPTED PACKET ==========`,
        // );
        // console.log(
        //   `[ClientConnection] Encrypted payload size: ${payload.length} bytes`,
        // );
        try {
          // Decrypt the payload (ciphertext) to get the inner plaintext packet (header + payload)
          // console.log(`[ClientConnection] Calling decryptPacket...`);
          const plaintext = new Uint8Array(decryptPacket(payload));
          // console.log(
          //   `[ClientConnection] Decryption complete, plaintext length: ${plaintext.length} bytes`,
          // );
          // console.error(
          //   `[ClientConnection] Decrypted ENCRYPTED packet, inner length: ${plaintext.length}`,
          // );

          // Parse the inner packet header
          const innerParsed = parsePacket(plaintext);
          // console.log(
          //   `[ClientConnection] Inner packet type: ${innerParsed.type}`,
          // );
          // console.log(
          //   `[ClientConnection] Inner packet payload size: ${innerParsed.length}`,
          // );
          // console.error(
          //   `[ClientConnection] Inner packet: type=${innerParsed.type} (${innerName}) len=${innerParsed.length}`,
          // );

          // Extract inner payload (skip inner header)
          const innerPayload = plaintext.slice(HEADER_SIZE);
          // console.log(
          //   `[ClientConnection] Inner payload extracted: ${innerPayload.length} bytes`,
          // );

          // Track inner packet types too
          const innerCount = (this.packetStats.get(innerParsed.type) || 0) + 1;
          this.packetStats.set(innerParsed.type, innerCount);

          // if (innerParsed.type === PacketType.ASCII_FRAME) {
          //   console.log(
          //     `[ClientConnection] ========== INNER PACKET IS ASCII_FRAME ==========`,
          //   );
          //   console.log(
          //     `[ClientConnection] Calling user callback with ASCII_FRAME...`,
          //   );
          // }

          // Call user callback with the decrypted inner packet
          this.onPacketCallback?.(innerParsed, innerPayload);

          // if (innerParsed.type === PacketType.ASCII_FRAME) {
          //   console.log(
          //     `[ClientConnection] ========== ASCII_FRAME CALLBACK COMPLETE ==========`,
          //   );
          // }
        } catch (error) {
          console.error(
            "[ClientConnection] ========== DECRYPTION ERROR ==========",
          );
          console.error(
            "[ClientConnection] Failed to decrypt ENCRYPTED packet:",
            error,
          );
          console.error(
            "[ClientConnection] Error stack:",
            error instanceof Error ? error.stack : String(error),
          );
          console.error("[ClientConnection] ========== END ERROR ==========");
        }
        return;
      }

      // Non-encrypted, non-handshake packet in connected state - pass through
      this.onPacketCallback?.(parsed, payload);
    } catch (error) {
      console.error("[ClientConnection] Failed to handle packet:", error);
    }
  }

  /**
   * Send a packet to server
   */
  sendPacket(packetType: number, payload: Uint8Array): void {
    if (!this.socket || !this.socket.isConnected()) {
      const name = packetTypeName(packetType);
      throw new Error(
        `Not connected to server (cannot send ${name} type=${packetType})`,
      );
    }

    const name = packetTypeName(packetType);
    console.log(
      `[ClientConnection] sendPacket() called: type=${packetType} (${name}), payload_size=${payload.length}`,
    );
    console.log(
      `[ClientConnection] Payload hex: [${Array.from(payload.slice(0, 32))
        .map((b) => `0x${b.toString(16).padStart(2, "0")}`)
        .join(" ")}]${payload.length > 32 ? "..." : ""}`,
    );

    try {
      const state = this.getState();
      console.log(
        `[ClientConnection] Current connection state: ${state} (${ConnectionState[state]})`,
      );
      console.error(
        `[ClientConnection] STATE_CHECK: about to send ${name}, state=${state} CONNECTED=${ConnectionState.CONNECTED} HANDSHAKE=${ConnectionState.HANDSHAKE}`,
      );

      if (state === ConnectionState.CONNECTED) {
        if (!this.usesApplicationEncryption) {
          this.socket.send(serializePacket(packetType, payload, 0));
          return;
        }
        // Matching TCP transport protocol: encrypt entire packet, wrap in PACKET_TYPE_ENCRYPTED
        // 1. Build plaintext packet (header + payload)
        const t0 = performance.now();
        const plaintextPacket = serializePacket(packetType, payload, 0);
        const t1 = performance.now();

        // 2. Encrypt the entire plaintext packet
        const ciphertext = encryptPacket(plaintextPacket);
        const t2 = performance.now();

        // 3. Wrap ciphertext in PACKET_TYPE_ENCRYPTED header
        const encryptedPacket = serializePacket(
          PacketType.ENCRYPTED,
          ciphertext,
          0,
        );
        const t3 = performance.now();
        this.socket.send(encryptedPacket);
        const t4 = performance.now();

        // Timing for IMAGE_FRAME packets (large frames)
        if (packetType === PacketType.IMAGE_FRAME) {
          console.log(
            `[WS_TIMING] Browser send ${name}: serialize=${(t1 - t0).toFixed(1)}ms encrypt=${(t2 - t1).toFixed(1)}ms wrap=${(t3 - t2).toFixed(1)}ms ws_send=${(t4 - t3).toFixed(1)}ms total=${(t4 - t0).toFixed(1)}ms size=${encryptedPacket.length}`,
          );
        }
      } else if (state === ConnectionState.HANDSHAKE) {
        // During handshake, send unencrypted ACIP packet
        // Delegate to sendUnencryptedAcipPacket for consistent handling
        return this.sendUnencryptedAcipPacket(packetType, payload);
      } else {
        // In ERROR, DISCONNECTED, or CONNECTING state - don't send
        console.error(
          `[ClientConnection] ❌ PACKET DROPPED: ${name} in state ${state} (${ConnectionState[state]})`,
        );
        console.error(
          `[ClientConnection] State values: DISCONNECTED=${ConnectionState.DISCONNECTED} CONNECTING=${ConnectionState.CONNECTING} HANDSHAKE=${ConnectionState.HANDSHAKE} CONNECTED=${ConnectionState.CONNECTED} ERROR=${ConnectionState.ERROR}`,
        );
        console.error(
          `[ClientConnection] Packet details: type=${packetType} size=${payload.length} name=${name}`,
        );
      }
    } catch (error) {
      console.error(`[ClientConnection] Failed to send ${name} packet:`, error);
      console.error(
        `[ClientConnection] Error stack:`,
        error instanceof Error ? error.stack : String(error),
      );
      throw error;
    }
  }

  /**
   * Send ACIP protocol control packets UNENCRYPTED
   * Used for CLIENT_CAPABILITIES and STREAM_START which must be sent before encryption is fully setup
   * This bypasses the encryption wrapper that's normally used for application-layer packets
   */
  sendUnencryptedAcipPacket(packetType: number, payload: Uint8Array): void {
    if (!this.socket || !this.socket.isConnected()) {
      const name = packetTypeName(packetType);
      throw new Error(
        `Not connected to server (cannot send ${name} type=${packetType})`,
      );
    }

    const name = packetTypeName(packetType);
    console.log(
      `[ClientConnection] sendUnencryptedAcipPacket() called: type=${packetType} (${name}), payload_size=${payload.length}`,
    );
    console.log(
      `[ClientConnection] Payload hex: [${Array.from(payload.slice(0, 32))
        .map((b) => `0x${b.toString(16).padStart(2, "0")}`)
        .join(" ")}]${payload.length > 32 ? "..." : ""}`,
    );

    try {
      const state = getConnectionState();
      console.log(
        `[ClientConnection] Sending unencrypted ACIP packet in state: ${state} (${ConnectionState[state]})`,
      );

      // Send the packet directly WITHOUT encryption
      // This is used for protocol control packets (CLIENT_CAPABILITIES, STREAM_START)
      // that must be sent as plain ACIP packets before crypto is fully established
      const packet = serializePacket(packetType, payload, 0);
      console.error(
        `[ClientConnection] >>> SEND unencrypted ACIP packet type=${packetType} (${name}) total_len=${packet.length} payload_len=${payload.length}`,
      );
      this.socket.send(packet);
      console.log(
        `[ClientConnection] Unencrypted ACIP ${name} packet sent to WebSocket`,
      );
    } catch (error) {
      console.error(
        `[ClientConnection] Failed to send unencrypted ACIP ${name} packet:`,
        error,
      );
      console.error(
        `[ClientConnection] Error stack:`,
        error instanceof Error ? error.stack : String(error),
      );
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
    return this.usesApplicationEncryption
      ? getConnectionState()
      : this.transportState;
  }

  /**
   * Disconnect and cleanup
   */
  disconnect(): void {
    console.log("[ClientConnection] Disconnecting...");
    this.isUserDisconnecting = true;
    this.transportState = ConnectionState.DISCONNECTED;

    if (this.socket) {
      this.socket.close();
      this.socket = null;
    }
    cleanupClientWasm();
    this.onStateChangeCallback?.(ConnectionState.DISCONNECTED);
  }
}
