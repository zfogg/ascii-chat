import {
  processClientEvent,
  drainClientOutputs,
  getClientModule,
  ConnectionState,
} from "../wasm/client";
import { AcipQuicProtocol } from "./AcipQuicProtocol";
import {
  createBrowserWebTransportBridge,
  type WebTransportBridge,
  type WebTransportPacket,
} from "./WebTransportBridge";

type Delivery = "reliable" | "datagram";
export interface WebTransportAcipCallbacks {
  /** Pass this packet to the existing WASM ACIP handshake state machine. */
  handshake(packet: Uint8Array): void;
  packet(packet: WebTransportPacket): void;
  ready(): void;
  keyframe(type: number, stream: number): void;
  datagramDropped?(type: number, error: Error): void;
  transportReady?(): void;
  failed(error: Error): void;
}

/** WebTransport wire mapping and the native ACIP-over-QUIC security barrier.
 * The owner initializes the shared client WASM context, starts its ACIP
 * handshake with sendHandshake(), and calls authenticated() when it succeeds. */
export class WebTransportAcipChannel {
  private readonly bridge: WebTransportBridge;
  private readonly protocol: AcipQuicProtocol;
  private closed = false;
  private automaticHandshake = false;

  constructor(
    private readonly url: string,
    private readonly callbacks: WebTransportAcipCallbacks,
    options?: WebTransportOptions,
    security?: ConstructorParameters<typeof AcipQuicProtocol>[0],
  ) {
    this.protocol = new AcipQuicProtocol(
      security ?? {
        init: () => {
          const result = processClientEvent({ type: "quic-init" });
          if (result.code)
            throw new Error(
              `QUIC security initialization failed (${result.code})`,
            );
        },
        tick: () => {
          const result = drainClientOutputs();
          if (result.code)
            throw new Error(`QUIC rekey failed (${result.code})`);
          return result.outputs ?? [];
        },
        seal: (flow, delivery, packet) => {
          const result = processClientEvent({
            type: "quic-packet",
            operation: "seal",
            flow,
            delivery: delivery === "datagram" ? 1 : 0,
            packet,
          });
          if (result.code || !result.bytes || result.sequence === undefined)
            throw new Error(`QUIC packet seal failed (${result.code})`);
          return result.bytes;
        },
        open: (flow, delivery, packet) => {
          const result = processClientEvent({
            type: "quic-packet",
            operation: "open",
            flow,
            delivery: delivery === "datagram" ? 1 : 0,
            packet,
          });
          if (result.code) {
            if (delivery === "datagram")
              throw new Error(`QUIC packet open failed (${result.code})`);
            throw new Error(`QUIC packet open failed (${result.code})`);
          }
          return {
            packet: result.bytes ?? new Uint8Array(),
            sequence: result.sequence ?? 0n,
          };
        },
      },
      {
        send: (flow, delivery, packet, deadline) =>
          this.bridge.send(flow, delivery, packet, deadline),
        serialize: (type, payload) => {
          const result = processClientEvent({
            type: "encode-packet",
            packetType: type,
            payload,
            clientId: 0,
          });
          if (result.code || !result.bytes)
            throw new Error(`Packet serialization failed (${result.code})`);
          return result.bytes;
        },
        handshake: (packet) => {
          if (this.automaticHandshake) this.receiveHandshake(packet);
          callbacks.handshake(packet);
        },
        packet: (packet) => callbacks.packet(packet),
        ready: () => callbacks.ready(),
        keyframe: (type, stream) => callbacks.keyframe(type, stream),
        datagramDropped: (type, error) =>
          callbacks.datagramDropped?.(type, error),
        failed: (error) => this.fail(error),
      },
    );
    this.bridge = createBrowserWebTransportBridge(
      url,
      (packet) => this.protocol.receive(packet),
      (error) => this.fail(error),
      options,
      () => callbacks.transportReady?.(),
    );
  }

  async connect(): Promise<void> {
    await this.bridge.connect();
  }
  /** Start the existing WASM ACIP handshake after the owner initializes the
   * client context, installs credentials, and generates its keypair. */
  async connectWithWasmHandshake(): Promise<void> {
    if (this.automaticHandshake || this.closed)
      throw new Error("QUIC handshake already started");
    const policy = processClientEvent({
      type: "resolve-crypto-policy",
      wss: new URL(this.url).protocol === "wss:",
      encryption: 1,
    });
    if (policy.code)
      throw new Error(`Crypto policy resolution failed (${policy.code})`);
    this.automaticHandshake = true;
    getClientModule()!.sendPacketCallback = (packet) => this.sendHandshake(packet);
    try {
      await this.bridge.connect();
      const result = processClientEvent({
        type: "begin-handshake",
        discovery: false,
        mode: 0,
      });
      if (result.code) throw new Error(`Handshake failed (${result.code})`);
    } catch (error) {
      this.close();
      throw error;
    }
  }

  private receiveHandshake(packet: Uint8Array): void {
    const nextResult = processClientEvent({
      type: "receive-handshake-packet",
      discovery: false,
      payload: packet,
    });
    if (nextResult.code)
      throw new Error(`Security handshake rejected (${nextResult.code})`);
    const next = nextResult.state;
    if (next === ConnectionState.CONNECTED) this.authenticated();
  }

  sendHandshake(packet: Uint8Array): void {
    this.protocol.sendHandshake(packet);
  }
  authenticated(): void {
    this.protocol.authenticated();
  }
  isReady(): boolean {
    return this.protocol.isReady();
  }
  getBufferedAmount(): number {
    return this.bridge.getBufferedAmount();
  }
  supportsDatagrams(): boolean {
    return this.bridge.supportsDatagrams();
  }
  send(
    type: number,
    payload: Uint8Array,
    delivery: Delivery = "reliable",
    deadline = 0,
  ): void {
    this.protocol.send(type, payload, delivery, deadline);
  }
  close(): void {
    if (this.closed) return;
    this.closed = true;
    this.protocol.close();
    this.bridge.close();
  }
  private fail(error: Error): void {
    if (this.closed) return;
    this.close();
    this.callbacks.failed(error);
  }
}
