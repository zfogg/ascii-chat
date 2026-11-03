import { serializeClientInitialization } from "./ClientInitialization";
import { OutboundQueue } from "./OutboundQueue";
import type { ClientSession } from "./Transport";
import { WebTransportAcipChannel } from "./WebTransportAcipChannel";
import {
  cleanupClientWasm,
  configureClientCrypto,
  ConnectionState,
  generateKeypair,
  initClientWasm,
  PacketType,
  parsePacket,
  serializePacket,
  setServerAddress,
  type ClientCryptoOptions,
  type ParsedPacket,
} from "../wasm/client";

export interface WebTransportClientOptions {
  serverUrl: string;
  width?: number;
  height?: number;
  applicationEncryption?: boolean;
  cryptoOptions?: ClientCryptoOptions;
  preferDatagrams?: boolean;
}

/** Direct browser session over HTTP/3 WebTransport and authenticated ACIP. */
export class WebTransportClientSession implements ClientSession {
  readonly transportType = "webtransport" as const;
  readonly canReconnect = true;
  private state = ConnectionState.DISCONNECTED;
  private channel: WebTransportAcipChannel | null = null;
  private outbound: OutboundQueue | null = null;
  private publicKey: string | null = null;
  private cancelled = false;
  private ownsWasm = false;
  private preparing = false;
  private connectPromise: Promise<void> | null = null;
  private droppedDatagrams = 0;
  private onState: ((state: ConnectionState) => void) | null = null;
  private onPacket:
    | ((packet: ParsedPacket, payload: Uint8Array) => void)
    | null = null;

  constructor(private readonly options: WebTransportClientOptions) {}

  connect(): Promise<void> {
    this.connectPromise ??= this.start();
    return this.connectPromise;
  }

  private async start(): Promise<void> {
    if (this.cancelled) throw new Error("WebTransport session is closed");
    this.setState(ConnectionState.CONNECTING);
    try {
      if (this.options.applicationEncryption === false)
        throw new Error("WebTransport requires ACIP application encryption");
      const prepared = await serializeClientInitialization(async () => {
        if (this.cancelled) return false;
        this.preparing = true;
        let succeeded = false;
        try {
          const init = {
            ...(this.options.width !== undefined
              ? { width: this.options.width }
              : {}),
            ...(this.options.height !== undefined
              ? { height: this.options.height }
              : {}),
          };
          await initClientWasm(init);
          this.ownsWasm = true;
          if (this.cancelled) {
            this.releaseWasm();
            return false;
          }
          await configureClientCrypto(this.options.cryptoOptions ?? {});
          if (this.cancelled) {
            this.releaseWasm();
            return false;
          }
          const publicKey = await generateKeypair();
          if (this.cancelled) {
            this.releaseWasm();
            return false;
          }
          const url = new URL(this.options.serverUrl);
          if (url.protocol !== "https:")
            throw new Error("WebTransport requires an HTTPS URL");
          setServerAddress(url.hostname, Number(url.port) || 443);
          this.publicKey = publicKey;
          succeeded = true;
          return true;
        } finally {
          this.preparing = false;
          if (this.cancelled || !succeeded) this.releaseWasm();
        }
      });
      if (!prepared || this.cancelled) return;
      const channel = new WebTransportAcipChannel(this.options.serverUrl, {
        handshake: () => {},
        ready: () => {
          if (this.cancelled) return;
          this.outbound = new OutboundQueue(
            () => channel.getBufferedAmount(),
            (wire) => {
              const type = new DataView(
                wire.buffer,
                wire.byteOffset,
                wire.byteLength,
              ).getUint16(8);
              const media =
                type === 2001 ||
                type === 4000 ||
                type === 4001 ||
                (type >= 3000 && type <= 3003);
              const datagram =
                media &&
                this.options.preferDatagrams !== false &&
                channel.supportsDatagrams();
              channel.send(
                type,
                wire.subarray(22),
                datagram ? "datagram" : "reliable",
                datagram ? performance.now() + 100 : 0,
              );
            },
            (error) => this.fail(error),
          );
          this.setState(ConnectionState.CONNECTED);
        },
        packet: ({ packet }) => {
          if (this.cancelled) return;
          const parsed = parsePacket(packet);
          this.onPacket?.(parsed, packet.slice(22));
        },
        keyframe: (type, stream) =>
          this.outbound?.requestKeyframe(type, stream),
        datagramDropped: () => {
          ++this.droppedDatagrams;
        },
        failed: (error) => this.fail(error),
      });
      this.channel = channel;
      await channel.connectWithWasmHandshake();
      if (this.cancelled) channel.close();
      else if (this.state === ConnectionState.ERROR)
        throw new Error("WebTransport handshake failed");
    } catch (error) {
      if (!this.cancelled)
        this.fail(error instanceof Error ? error : new Error(String(error)));
      throw error;
    }
  }

  getPublicKey(): string | null {
    return this.publicKey;
  }
  getState(): ConnectionState {
    return this.state;
  }
  onStateChange(callback: (state: ConnectionState) => void): void {
    this.onState = callback;
  }
  onPacketReceived(
    callback: (packet: ParsedPacket, payload: Uint8Array) => void,
  ): void {
    this.onPacket = callback;
  }
  getBufferedAmount(): number {
    return (
      (this.channel?.getBufferedAmount() ?? 0) +
      (this.outbound?.snapshot().applicationBytes ?? 0)
    );
  }
  getUploadFeedback() {
    const snapshot = this.outbound?.snapshot();
    return {
      bufferedBytes: this.getBufferedAmount(),
      oldestFrameAgeMs: snapshot?.oldestFrameAgeMs ?? 0,
      droppedFrames: (snapshot?.dropped ?? 0) + this.droppedDatagrams,
    };
  }
  needsKeyframe(type: number): boolean {
    return this.outbound?.needsKeyframe(type) ?? true;
  }
  sendPacket(type: number, payload: Uint8Array): void {
    if (this.state !== ConnectionState.CONNECTED || !this.outbound)
      throw new Error("Security negotiation is not complete");
    const admission = this.outbound.submit(serializePacket(type, payload, 0));
    if (
      admission === "congested" &&
      type !== PacketType.IMAGE_FRAME &&
      type !== PacketType.ASCII_FRAME &&
      type !== PacketType.IMAGE_FRAME_H264 &&
      type !== PacketType.IMAGE_FRAME_H265
    )
      throw new Error("Reliable outbound queue is congested");
  }
  sendUnencryptedAcipPacket(type: number, payload: Uint8Array): void {
    this.sendPacket(type, payload);
  }
  disconnect(): void {
    if (this.cancelled) return;
    this.cancelled = true;
    this.outbound?.close();
    this.outbound = null;
    this.channel?.close();
    this.channel = null;
    this.publicKey = null;
    if (!this.preparing) this.releaseWasm();
    this.setState(ConnectionState.DISCONNECTED);
  }
  private releaseWasm(): void {
    if (!this.ownsWasm) return;
    this.ownsWasm = false;
    cleanupClientWasm();
  }
  private setState(value: ConnectionState): void {
    this.state = value;
    this.onState?.(value);
  }
  private fail(error: Error): void {
    if (this.cancelled || this.state === ConnectionState.ERROR) return;
    this.outbound?.close();
    this.outbound = null;
    this.channel?.close();
    this.channel = null;
    this.releaseWasm();
    console.error("[WebTransport] Connection failed:", error);
    this.setState(ConnectionState.ERROR);
  }
}
