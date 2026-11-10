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
  configureBrowserCapabilities,
  drainClientOutputs,
  initClientWasm,
  cleanupClientWasm,
  generateKeypair,
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
    const hevc = await this.probeHevcSupport();
    if (this.isUserDisconnecting) {
      cleanupClientWasm();
      return false;
    }
    configureBrowserCapabilities({
      webtransport: typeof WebTransport !== "undefined",
      datagrams:
        typeof WebTransport !== "undefined" &&
        "datagrams" in WebTransport.prototype,
      hevc,
      webrtc: typeof RTCPeerConnection !== "undefined",
    });
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
    return true;
  }

  private async probeHevcSupport(): Promise<boolean> {
    if (
      typeof VideoEncoder === "undefined" ||
      typeof VideoEncoder.isConfigSupported !== "function"
    )
      return false;
    try {
      const support = await VideoEncoder.isConfigSupported({
        codec: "hev1.1.6.L120.B0",
        width: 640,
        height: 480,
        bitrate: 1_000_000,
        framerate: 30,
        latencyMode: "realtime",
        hevc: { format: "annexb" },
      } as VideoEncoderConfig & { hevc: { format: "annexb" } });
      return support.supported === true;
    } catch {
      return false;
    }
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
        if (result.code) throwWasmError(result.code, "QUIC rekey failed");
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
      if (handshake.code) throwWasmError(handshake.code, "Handshake failed");
      for (const { packet } of handshake.outputs ?? [])
        this.socket?.send(packet);
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
      const packetResult = processClientEvent({
        type: "receive-transport-packet",
        packet: rawPacket,
        discovery: Boolean(this.options.discoveryHandshake),
      });
      if (packetResult.code)
        throwWasmError(packetResult.code, "Packet processing failed");
      for (const { packet } of packetResult.outputs ?? [])
        this.socket?.send(packet);
      const next = packetResult.state;
      if (next !== undefined && next !== this.transportState) {
        if (next === ConnectionState.CONNECTED) {
          this.wasEverConnected = true;
          this.transportState = ConnectionState.CONNECTED;
          setNetworkErrorReporter(this.reportToServer);
        }
        this.onStateChangeCallback?.(next);
      }
      if (!packetResult.packet || !packetResult.payload) return;
      const parsed = packetResult.packet;
      const payload = new Uint8Array(packetResult.payload);
      if (parsed.type === PacketType.ERROR_MESSAGE)
        this.handleRemoteError(payload);
      else this.onPacketCallback?.(parsed, payload);
      return;
    } catch (error) {
      console.error("[ClientConnection] Failed to handle packet:", error);
      if (isReportedWasmError(error)) forwardJavaScriptError(error);
      else
        reportJavaScriptError(
          error instanceof Error ? error : new Error(String(error)),
        );
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
      type: "encode-packet",
      packetType,
      payload,
      clientId: 0,
    });
    if (result.code || !result.bytes)
      throwWasmError(result.code, "Serialization failed");
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
        const packetResult = processClientEvent({
          type: "send-packet",
          packetType,
          payload,
          clientId: 0,
          encrypt: this.usesApplicationEncryption,
        });
        if (packetResult.code || !packetResult.bytes)
          throwWasmError(packetResult.code, "Packet send failed");
        this.socket.send(packetResult.bytes);
        return;
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
    if (!this.socket || !this.socket.isConnected())
      throw new Error("Not connected to server");
    const result = processClientEvent({
      type: "send-packet",
      packetType,
      payload,
      clientId: 0,
      encrypt: false,
    });
    if (result.code || !result.bytes)
      throwWasmError(result.code, "Packet send failed");
    this.socket.send(result.bytes);
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
