import { OutboundQueue } from "./OutboundQueue";
/**
 * High-level client connection manager
 * Orchestrates WebSocket, WASM crypto, and packet handling
 */

import { SocketBridge } from "./SocketBridge";
import type { PacketTransport } from "./Transport";
import {
  getClientModule,
  resolveCryptoPolicy,
  initClientWasm,
  cleanupClientWasm,
  generateKeypair,
  setServerAddress,
  registerSendPacketCallback,
  encryptPacket,
  decryptPacket,
  parsePacket,
  serializePacket,
  getConnectionState,
  configureClientCrypto,
  ConnectionState,
  PacketType,
  packetTypeName,
  type ParsedPacket,
  type ClientInitOptions,
  type ClientCryptoOptions,
  isReportedWasmError,
  throwWasmError,
} from "../wasm/client";
import {
  forwardJavaScriptError,
  reportJavaScriptError,
  reportProtocolError,
  setNetworkErrorReporter,
  type BrowserErrorReport,
} from "../errors/errorBus";
import { decodeRemoteError, encodeBrowserError } from "./errorProtocol";

export interface ClientConnectionOptions {
  serverUrl: string;
  applicationEncryption?: boolean;
  discoveryHandshake?: boolean;
  cryptoOptions?: ClientCryptoOptions;
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
  private cryptoMode = 0;
  private discoveryAccepted = false;
  private get usesApplicationEncryption(): boolean {
    return (this.cryptoMode & 1) !== 0;
  }
  private clientPublicKey: string | null = null;
  private onStateChangeCallback: ConnectionStateChangeCallback | null = null;
  private onPacketCallback: PacketReceivedCallback | null = null;
  private isUserDisconnecting = false;
  private wasEverConnected = false;
  private wasmReinitInProgress = false;
  private deferredPackets: Uint8Array[] = [];
  private socketHasEverOpened = false; // Track if this socket instance has ever opened

  constructor(private options: ClientConnectionOptions) {}

  private reportToServer = (report: BrowserErrorReport): void => {
    if (this.transportState !== ConnectionState.CONNECTED) return;
    try {
      this.sendPacket(PacketType.ERROR_MESSAGE, encodeBrowserError(report));
    } catch (error) {
      console.warn("[ClientConnection] Could not report browser error:", error);
    }
  };

  private handleRemoteError(payload: Uint8Array): void {
    try {
      const error = decodeRemoteError(payload);
      reportProtocolError(error.code, error.message);
    } catch (error) {
      reportProtocolError(49, (error as Error).message);
    }
  }

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
    await configureClientCrypto(this.options.cryptoOptions || {});
    this.cryptoMode = resolveCryptoPolicy(
      this.options.serverUrl,
      this.options.applicationEncryption,
    );
    console.log("[ClientConnection] WASM init complete");

    // Register callback so WASM can send raw packets back through WebSocket
    registerSendPacketCallback((rawPacket: Uint8Array) => {
      if (!this.socket) {
        console.error(
          "[ClientConnection] Cannot send packet - socket not connected",
        );
        return;
      }
      this.socket.send(rawPacket);
    });

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
        console.log(
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
          this.outbound?.close();
          this.outbound = null;
          if (
            !this.isUserDisconnecting &&
            this.transportState === ConnectionState.ERROR
          ) {
            this.onStateChangeCallback?.(ConnectionState.ERROR);
            return;
          }
          this.transportState =
            this.isUserDisconnecting || this.wasEverConnected
              ? ConnectionState.DISCONNECTED
              : ConnectionState.ERROR;
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
            this.onStateChangeCallback?.(ConnectionState.ERROR);
          }
        }
      },
    });

    console.log("[ClientConnection] Waiting for WebSocket to connect...");
    await this.socket.connect();
  }

  private async handleSocketOpen(): Promise<void> {
    this.discoveryAccepted = false;
    const isReconnection = this.socketHasEverOpened;
    this.socketHasEverOpened = true;

    if (isReconnection) {
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
        await configureClientCrypto(this.options.cryptoOptions || {});
        this.cryptoMode = resolveCryptoPolicy(
          this.options.serverUrl,
          this.options.applicationEncryption,
        );
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

    try {
      const module = getClientModule();
      if (!module) throw new Error("WASM client unavailable");
      const result = this.options.discoveryHandshake
        ? module._client_acds_handshake_begin(this.cryptoMode)
        : module._client_handshake_begin();
      if (result)
        throwWasmError(result, "Could not begin security negotiation");
    } catch (error) {
      if (isReportedWasmError(error)) forwardJavaScriptError(error);
      else reportJavaScriptError(error);
      this.socket.close();
      this.transportState = ConnectionState.ERROR;
      this.onStateChangeCallback?.(ConnectionState.ERROR);
      return;
    }
    // Discovery's TLS-only protocol has no version acknowledgement.
    if (this.options.discoveryHandshake && this.cryptoMode === 0) {
      this.transportState = ConnectionState.CONNECTED;
      this.wasEverConnected = true;
      setNetworkErrorReporter(this.reportToServer);
      this.onStateChangeCallback?.(ConnectionState.CONNECTED);
      return;
    }

    this.transportState = ConnectionState.CONNECTING;
    this.onStateChangeCallback?.(ConnectionState.CONNECTING);
  }

  /**
   * Handle incoming packet from WebSocket
   */
  private handlePacket(rawPacket: Uint8Array): void {
    // Defer packet handling if WASM is being reinitialized
    if (this.wasmReinitInProgress) {
      this.deferredPackets.push(rawPacket);
      return;
    }

    try {
      // Parse packet header
      const parsed = parsePacket(rawPacket);

      // Extract payload (skip header)
      const HEADER_SIZE = 22; // sizeof(packet_header_t): magic(8) + type(2) + length(4) + crc32(4) + client_id(4)
      const payload = rawPacket.slice(HEADER_SIZE);

      if (parsed.type === PacketType.ERROR_MESSAGE) {
        this.handleRemoteError(payload);
        throw new Error("Remote endpoint reported a protocol error");
      }

      // TLS-only discovery starts application traffic without a version ACK.
      // A policy rejection must stop retries before its first application reply.
      if (
        this.options.discoveryHandshake &&
        !this.discoveryAccepted &&
        parsed.type === PacketType.ERROR_MESSAGE
      )
        throw new Error("Discovery security policy rejected");

      if (this.transportState !== ConnectionState.CONNECTED) {
        const module = getClientModule()!;
        const ptr = module._malloc(payload.length || 1);
        try {
          if (!ptr) throw new Error("Could not allocate handshake packet");
          module.HEAPU8.set(payload, ptr);
          const result = this.options.discoveryHandshake
            ? module._client_acds_handshake_receive(
                parsed.type,
                ptr,
                payload.length,
              )
            : module._client_handshake_receive(
                parsed.type,
                ptr,
                payload.length,
              );
          if (result) throwWasmError(result, "Security handshake rejected");
          const next = getConnectionState();
          if (next === ConnectionState.CONNECTED) {
            this.discoveryAccepted = true;
            this.wasEverConnected = true;
            this.transportState = ConnectionState.CONNECTED;
            setNetworkErrorReporter(this.reportToServer);
          }
          this.onStateChangeCallback?.(next);
        } finally {
          module._free(ptr);
        }
        return;
      }

      this.discoveryAccepted = true;

      // For non-handshake packets during handshake, log a warning
      const state = this.getState();
      if (state !== ConnectionState.CONNECTED) {
        // console.error(
        //   `[ClientConnection] *** Got non-handshake packet ${name} (${parsed.type}) while in state
        //   ${ConnectionState[state]} (${state}) - ignoring`,
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
          if (innerParsed.type === PacketType.ERROR_MESSAGE) {
            this.handleRemoteError(innerPayload);
            throw new Error("Remote endpoint reported a protocol error");
          }
          // console.log(
          //   `[ClientConnection] Inner payload extracted: ${innerPayload.length} bytes`,
          // );

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
          throw error;
        }
        return;
      }

      if (this.usesApplicationEncryption)
        throw new Error("Plaintext application packet on encrypted connection");

      // Auth-only and TLS-only sessions send plaintext ACIP payloads.
      this.onPacketCallback?.(parsed, payload);
    } catch (error) {
      console.error("[ClientConnection] Failed to handle packet:", error);
      if (
        !(
          error instanceof Error &&
          error.message.startsWith("Remote endpoint reported")
        )
      ) {
        if (isReportedWasmError(error)) forwardJavaScriptError(error);
        else reportJavaScriptError(error);
      }
      if (!this.isUserDisconnecting) {
        this.socket?.close();
        this.transportState = ConnectionState.ERROR;
        this.onStateChangeCallback?.(ConnectionState.ERROR);
      }
    }
  }

  /**
   * Send a packet to server
   */
  getBufferedAmount(): number {
    return (
      (this.socket?.getBufferedAmount?.() ?? 0) +
      (this.outbound?.snapshot().applicationBytes ?? 0)
    );
  }

  getUploadFeedback() {
    const snapshot = this.outbound?.snapshot();
    return {
      bufferedBytes: this.getBufferedAmount(),
      oldestFrameAgeMs: snapshot?.oldestFrameAgeMs ?? 0,
      droppedFrames: snapshot?.dropped ?? 0,
    };
  }

  private outbound: OutboundQueue | null = null;

  needsKeyframe(type: number): boolean {
    return this.outbound?.needsKeyframe(type) ?? true;
  }

  sendPacket(packetType: number, payload: Uint8Array): void {
    if (this.transportState !== ConnectionState.CONNECTED)
      throw new Error("Security negotiation is not complete");
    if (!this.outbound)
      this.outbound = new OutboundQueue(
        () => this.socket?.getBufferedAmount?.() ?? 0,
        (packet) => {
          const type = new DataView(packet.buffer, packet.byteOffset).getUint16(
            8,
            false,
          );
          this.sendPacketNow(type, packet.slice(22));
        },
        (error) => {
          this.socket?.close();
          this.transportState = ConnectionState.ERROR;
          this.onStateChangeCallback?.(ConnectionState.ERROR);
          console.error(error);
        },
      );
    const admission = this.outbound.submit(
      serializePacket(packetType, payload, 0),
    );
    if (
      admission === "congested" &&
      packetType !== PacketType.IMAGE_FRAME &&
      packetType !== PacketType.ASCII_FRAME &&
      packetType !== PacketType.IMAGE_FRAME_H264 &&
      packetType !== PacketType.IMAGE_FRAME_H265
    )
      throw new Error("Reliable outbound queue is congested");
  }

  private sendPacketNow(packetType: number, payload: Uint8Array): void {
    if (this.transportState !== ConnectionState.CONNECTED)
      throw new Error("Security negotiation is not complete");
    if (!this.socket || !this.socket.isConnected()) {
      const name = packetTypeName(packetType);
      throw new Error(
        `Not connected to server (cannot send ${name} type=${packetType})`,
      );
    }

    const name = packetTypeName(packetType);

    try {
      const state = this.getState();

      if (state === ConnectionState.CONNECTED) {
        if (!this.usesApplicationEncryption) {
          this.socket.send(serializePacket(packetType, payload, 0));
          return;
        }
        // Matching TCP transport protocol: encrypt entire packet, wrap in PACKET_TYPE_ENCRYPTED
        // 1. Build plaintext packet (header + payload)
        const plaintextPacket = serializePacket(packetType, payload, 0);

        // 2. Encrypt the entire plaintext packet
        const ciphertext = encryptPacket(plaintextPacket);

        // 3. Wrap ciphertext in PACKET_TYPE_ENCRYPTED header
        const encryptedPacket = serializePacket(
          PacketType.ENCRYPTED,
          ciphertext,
          0,
        );

        this.socket.send(encryptedPacket);
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
          `[ClientConnection] State values: DISCONNECTED=${ConnectionState.DISCONNECTED} CONNECTING=${
            ConnectionState.CONNECTING
          } HANDSHAKE=${ConnectionState.HANDSHAKE} CONNECTED=${
            ConnectionState.CONNECTED
          } ERROR=${ConnectionState.ERROR}`,
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

  /** Setup packets follow the same negotiated policy as media packets. */
  sendUnencryptedAcipPacket(packetType: number, payload: Uint8Array): void {
    this.sendPacket(packetType, payload);
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
    if (this.transportState === ConnectionState.ERROR)
      return ConnectionState.ERROR;
    return this.transportState;
  }

  /**
   * Disconnect and cleanup
   */
  disconnect(): void {
    console.log("[ClientConnection] Disconnecting...");
    this.outbound?.close();
    this.outbound = null;
    this.isUserDisconnecting = true;
    this.transportState = ConnectionState.DISCONNECTED;
    setNetworkErrorReporter(null);

    if (this.socket) {
      this.socket.close();
      this.socket = null;
    }
    cleanupClientWasm();
    this.onStateChangeCallback?.(ConnectionState.DISCONNECTED);
  }
}
