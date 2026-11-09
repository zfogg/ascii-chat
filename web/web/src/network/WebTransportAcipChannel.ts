import {
  initPacketSecurity,
  tickPacketSecurity,
  processSecurePacket,
  serializePacket,
  beginClientHandshake,
  receiveClientHandshake,
  registerSendPacketCallback,
  resolveCryptoPolicy,
  parsePacket,
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
        init: initPacketSecurity,
        tick: () => tickPacketSecurity(),
        seal: (flow, delivery, packet) =>
          processSecurePacket(
            "seal",
            flow,
            delivery === "datagram" ? 1 : 0,
            packet,
          ).packet,
        open: (flow, delivery, packet) =>
          processSecurePacket(
            "open",
            flow,
            delivery === "datagram" ? 1 : 0,
            packet,
            delivery === "datagram",
          ),
      },
      {
        send: (flow, delivery, packet, deadline) =>
          this.bridge.send(flow, delivery, packet, deadline),
        serialize: (type, payload) => serializePacket(type, payload, 0),
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
    resolveCryptoPolicy(this.url, true);
    this.automaticHandshake = true;
    registerSendPacketCallback((packet) => this.sendHandshake(packet));
    try {
      await this.bridge.connect();
      beginClientHandshake(false);
    } catch (error) {
      this.close();
      throw error;
    }
  }

  private receiveHandshake(packet: Uint8Array): void {
    const parsed = parsePacket(packet);
    const payload = packet.subarray(22);
    const next = receiveClientHandshake(false, parsed.type, payload);
    if (next === ConnectionState.CONNECTED)
      this.authenticated();
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
