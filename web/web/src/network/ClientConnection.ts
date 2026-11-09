import { OutboundQueue } from "./OutboundQueue";
import { serializeClientInitialization } from "./ClientInitialization";
/**
 * High-level client connection manager
 * Orchestrates WebSocket, WASM crypto, and packet handling
 */

import { SocketBridge } from "./SocketBridge";
import type { PacketTransport } from "./Transport";
import {
  processClientEvent,
  drainClientOutputs,
  initClientWasm,
  cleanupClientWasm,
  generateKeypair,
  getClientModule,
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
  setNetworkErrorReporter,
  type BrowserErrorReport,
} from "../errors/errorBus";
import {
  presentRemoteError,
  RemoteErrorAction,
  encodeBrowserError,
} from "./errorProtocol";

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
  private socket:
    | (PacketTransport & {
        connect(): Promise<void>;
        reconnect(): void;
      })
    | null = null;
  private transportState = ConnectionState.DISCONNECTED;
  private cryptoMode = 0;
  private securityTimer: ReturnType<typeof setInterval> | null = null;
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
  private retryingRemoteError = false;
  private connectPromise: Promise<void> | null = null;

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
    const action = presentRemoteError(payload);
    if (action === RemoteErrorAction.Continue) return;
    this.outbound?.close();
    this.outbound = null;
    if (action === RemoteErrorAction.Retry) {
      this.retryingRemoteError = true;
      this.transportState = ConnectionState.CONNECTING;
      this.onStateChangeCallback?.(ConnectionState.CONNECTING);
      this.socket?.reconnect();
    } else {
      this.socket?.close();
      this.transportState = ConnectionState.ERROR;
      this.onStateChangeCallback?.(ConnectionState.ERROR);
    }
  }

  /**
   * Initialize WASM and connect to server
   */
  connect(): Promise<void> {
    this.connectPromise ??= this.start();
    return this.connectPromise;
  }

  private async start(): Promise<void> {
    try {
      if (!(await this.initializeWasm())) return;
      await this.connectSocket();
    } catch (error) {
      if (!this.isUserDisconnecting) {
        this.disconnect();
        this.transportState = ConnectionState.ERROR;
        this.onStateChangeCallback?.(ConnectionState.ERROR);
      }
      throw error;
    }
  }

  // Initial startup and reconnect publish crypto and callbacks in the same order.
  // Cancellation is checked after every asynchronous boundary before publishing.
  private initializeWasm(): Promise<boolean> {
    return serializeClientInitialization(() => this.prepareWasm());
  }

  private async prepareWasm(): Promise<boolean> {
    if (this.isUserDisconnecting) return false;
    const initOptions: ClientInitOptions = { ...this.options.wasmOptions };
    if (this.options.width !== undefined)
      initOptions.width = this.options.width;
    if (this.options.height !== undefined)
      initOptions.height = this.options.height;
    await initClientWasm(initOptions);
    if (this.isUserDisconnecting) {
      cleanupClientWasm();
      return false;
    }
    await configureClientCrypto(this.options.cryptoOptions || {});
    if (this.isUserDisconnecting) {
      cleanupClientWasm();
      return false;
    }
    const policy = processClientEvent({
      type: "resolve-crypto-policy",
      wss: new URL(this.options.serverUrl).protocol === "wss:",
      encryption:
        this.options.applicationEncryption === undefined
          ? -1
          : this.options.applicationEncryption
            ? 1
            : 0,
    });
    if (policy.code || policy.mode === undefined)
      throwWasmError(policy.code || -1, "Invalid crypto policy");
    this.cryptoMode = policy.mode;
    const publicKey = await generateKeypair();
    if (this.isUserDisconnecting) {
      cleanupClientWasm();
      return false;
    }
    this.clientPublicKey = publicKey;
    const url = new URL(this.options.serverUrl);
    const serverResult = processClientEvent({
      type: "set-server",
      host: url.hostname,
      port: parseInt(url.port) || (url.protocol === "wss:" ? 443 : 27226),
    });
    if (serverResult.code)
      throwWasmError(serverResult.code, "Failed to set server address");
    getClientModule()!.sendPacketCallback = (rawPacket: Uint8Array) => {
      this.socket?.send(rawPacket);
    };
    return true;
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
          if (this.securityTimer) clearInterval(this.securityTimer);
          this.securityTimer = null;
          this.outbound?.close();
          this.outbound = null;
          // A rejection may arrive before the first successful handshake.
          // Keep that retry in progress instead of publishing a fatal failure.
          if (this.retryingRemoteError) return;
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
    if (this.securityTimer) clearInterval(this.securityTimer);
    this.securityTimer = setInterval(() => {
      if (
        this.transportState !== ConnectionState.CONNECTED ||
        !this.usesApplicationEncryption
      )
        return;
      try {
        const result = drainClientOutputs();
        if (result.code) throw new Error(`QUIC rekey failed (${result.code})`);
        for (const { packet } of result.outputs ?? [])
          this.socket?.send(packet);
      } catch (error) {
        this.disconnect();
        this.transportState = ConnectionState.ERROR;
        this.onStateChangeCallback?.(ConnectionState.ERROR);
        console.error("[ClientConnection] Security failed:", error);
      }
    }, 100);
    this.retryingRemoteError = false;
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

      try {
        if (!(await this.initializeWasm())) return;

        const deferred = this.deferredPackets;
        this.deferredPackets = [];
        this.wasmReinitInProgress = false;
        deferred.forEach((packet) => this.handlePacket(packet));
      } catch (error) {
        this.wasmReinitInProgress = false;
        console.error("[ClientConnection] WASM reinit failed:", error);
        this.socket?.close();
        cleanupClientWasm();
        this.deferredPackets = [];
        this.transportState = ConnectionState.ERROR;
        this.onStateChangeCallback?.(ConnectionState.ERROR);
        return;
      }
    }

    if (this.isUserDisconnecting || !this.socket?.isConnected()) return;

    try {
      const handshake = processClientEvent({
        type: "begin-handshake",
        discovery: this.options.discoveryHandshake,
        mode: this.cryptoMode,
      });
      if (handshake.code)
        throw new Error(`Handshake failed (${handshake.code})`);
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
    if (
      this.isUserDisconnecting ||
      this.retryingRemoteError ||
      this.transportState === ConnectionState.ERROR
    )
      return;
    // Defer packet handling if WASM is being reinitialized
    if (this.wasmReinitInProgress) {
      this.deferredPackets.push(rawPacket);
      return;
    }

    try {
      // Parse packet header
      const parsedResult = processClientEvent({
        type: "receive-packet",
        packet: rawPacket,
      });
      if (parsedResult.code || !parsedResult.packet)
        throw new Error(`Packet parse failed (${parsedResult.code})`);
      const parsed = parsedResult.packet;

      // Extract payload (skip header)
      const HEADER_SIZE = 22; // sizeof(packet_header_t): magic(8) + type(2) + length(4) + crc32(4) + client_id(4)
      const payload = rawPacket.slice(HEADER_SIZE);

      if (parsed.type === PacketType.ERROR_MESSAGE) {
        this.handleRemoteError(payload);
        return;
      }

      if (this.transportState !== ConnectionState.CONNECTED) {
        const handshake = processClientEvent({
          type: "receive-handshake",
          discovery: Boolean(this.options.discoveryHandshake),
          packetType: parsed.type,
          payload,
        });
        if (handshake.code)
          throw new Error(`Handshake rejected (${handshake.code})`);
        const next = handshake.state;
        if (next === ConnectionState.CONNECTED) {
          this.wasEverConnected = true;
          this.transportState = ConnectionState.CONNECTED;
          setNetworkErrorReporter(this.reportToServer);
        }
        this.onStateChangeCallback?.(next);
        return;
      }

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
          const decryptResult = processClientEvent({
            type: "crypt",
            encrypt: false,
            packet: payload,
          });
          if (decryptResult.code || !decryptResult.bytes)
            throw new Error(`Decryption failed (${decryptResult.code})`);
          const plaintext = new Uint8Array(decryptResult.bytes);
          if (!plaintext.length) return;
          // console.log(
          //   `[ClientConnection] Decryption complete, plaintext length: ${plaintext.length} bytes`,
          // );
          // console.error(
          //   `[ClientConnection] Decrypted ENCRYPTED packet, inner length: ${plaintext.length}`,
          // );

          // Parse the inner packet header
          const innerParsedResult = processClientEvent({
            type: "receive-packet",
            packet: plaintext,
          });
          if (innerParsedResult.code || !innerParsedResult.packet)
            throw new Error(
              `Inner packet parse failed (${innerParsedResult.code})`,
            );
          const innerParsed = innerParsedResult.packet;
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
            return;
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
      if (isReportedWasmError(error)) forwardJavaScriptError(error);
      else reportJavaScriptError(error);
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
    const result = processClientEvent({
      type: "serialize",
      packetType,
      payload,
      clientId: 0,
    });
    if (result.code || !result.bytes)
      throw new Error(`Serialization failed (${result.code})`);
    const admission = this.outbound.submit(result.bytes);
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
          const plainResult = processClientEvent({
            type: "serialize",
            packetType,
            payload,
            clientId: 0,
          });
          if (plainResult.code || !plainResult.bytes)
            throw new Error(`Serialization failed (${plainResult.code})`);
          this.socket.send(plainResult.bytes);
          return;
        }
        // Matching TCP transport protocol: encrypt entire packet, wrap in PACKET_TYPE_ENCRYPTED
        // 1. Build plaintext packet (header + payload)
        const plaintextResult = processClientEvent({
          type: "serialize",
          packetType,
          payload,
          clientId: 0,
        });
        if (plaintextResult.code || !plaintextResult.bytes)
          throw new Error(`Serialization failed (${plaintextResult.code})`);
        const plaintextPacket = plaintextResult.bytes;

        // 2. Encrypt the entire plaintext packet
        const encryptResult = processClientEvent({
          type: "crypt",
          encrypt: true,
          packet: plaintextPacket,
        });
        if (encryptResult.code || !encryptResult.bytes)
          throw new Error(`Encryption failed (${encryptResult.code})`);
        const ciphertext = encryptResult.bytes;

        // 3. Wrap ciphertext in PACKET_TYPE_ENCRYPTED header
        const encryptedResult = processClientEvent({
          type: "serialize",
          packetType: PacketType.ENCRYPTED,
          payload: ciphertext,
          clientId: 0,
        });
        if (encryptedResult.code || !encryptedResult.bytes)
          throw new Error(`Serialization failed (${encryptedResult.code})`);
        const encryptedPacket = encryptedResult.bytes;

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
    if (this.isUserDisconnecting) return;
    console.log("[ClientConnection] Disconnecting...");
    this.outbound?.close();
    this.outbound = null;
    if (this.securityTimer) clearInterval(this.securityTimer);
    this.securityTimer = null;
    this.isUserDisconnecting = true;
    this.transportState = ConnectionState.DISCONNECTED;
    setNetworkErrorReporter(null);

    if (this.socket) {
      this.socket.close();
      this.socket = null;
    }
    this.deferredPackets = [];
    this.clientPublicKey = null;
    cleanupClientWasm();
    this.onStateChangeCallback?.(ConnectionState.DISCONNECTED);
  }
}
