import type { AcipFlow } from "./AcipQuicStreamDecoder";
import type { WebTransportPacket } from "./WebTransportBridge";

type Delivery = "reliable" | "datagram";
type Security = {
  tick?(): { flow: AcipFlow; packet: Uint8Array }[];
  init(): void;
  seal(flow: AcipFlow, delivery: Delivery, packet: Uint8Array): Uint8Array;
  open(
    flow: AcipFlow,
    delivery: Delivery,
    packet: Uint8Array,
  ): { packet: Uint8Array; sequence: bigint };
};
type Callbacks = {
  send(
    flow: AcipFlow,
    delivery: Delivery,
    packet: Uint8Array,
    deadline?: number,
  ): Promise<void>;
  serialize(type: number, payload: Uint8Array): Uint8Array;
  handshake(packet: Uint8Array): void;
  packet(packet: WebTransportPacket): void;
  ready(): void;
  keyframe(type: number, stream: number): void;
  datagramDropped?(type: number, error: Error): void;
  failed(error: Error): void;
};

const ENCRYPTED = 1200;
const READY = 1110;
const KEYFRAME_REQUEST = 1111;
const HEADER = 22;
const READY_BYTES = new Uint8Array([65, 67, 81, 82, 1]);
const ACK_BYTES = new Uint8Array([65, 67, 81, 65, 1]);
const same = (a: Uint8Array, b: Uint8Array) =>
  a.length === b.length && a.every((byte, index) => byte === b[index]);
const typeOf = (packet: Uint8Array) => {
  if (packet.length < HEADER) throw new Error("Truncated ACIP packet");
  return new DataView(
    packet.buffer,
    packet.byteOffset,
    packet.byteLength,
  ).getUint16(8);
};
const handshakeType = (type: number) =>
  type === 1 || type === 1000 || type === 2003 || (type >= 1100 && type < 1109);
const flowOf = (type: number): AcipFlow =>
  type === 2001 || type === 4000 || type === 4001
    ? 1
    : type >= 3000 && type <= 3003
      ? 2
      : 0;

/** ACIP security and readiness on top of the browser WebTransport wire bridge.
 * The caller runs the existing ACIP handshake and calls authenticated() only
 * after its identity and encryption policy has succeeded. */
export class AcipQuicProtocol {
  private rekeyTimer: ReturnType<typeof setInterval> | undefined;
  private securityActive = false;
  private peerReady = false;
  private acknowledged = false;
  private closed = false;
  private early: Uint8Array | null = null;
  private lastVisual = 0n;
  private visualCodec = 0;
  private waitingKeyframe = false;
  private requestedKeyframe = false;

  constructor(
    private readonly security: Security,
    private readonly callbacks: Callbacks,
  ) {}

  isReady(): boolean {
    return this.acknowledged && !this.closed;
  }

  /** Handshake packets use reliable control and precede the security barrier. */
  sendHandshake(packet: Uint8Array): void {
    if (this.closed || this.securityActive || !handshakeType(typeOf(packet)))
      throw new Error("Invalid QUIC handshake packet");
    this.queue(0, "reliable", packet);
  }

  authenticated(): void {
    if (this.closed || this.securityActive)
      throw new Error("Invalid QUIC security transition");
    try {
      this.security.init();
      this.securityActive = true;
      this.sendInternal(READY, READY_BYTES);
      if (this.early) {
        const early = this.early;
        this.early = null;
        this.receive({ flow: 0, delivery: "reliable", packet: early });
      }
    } catch (error) {
      this.fail(error);
      throw error;
    }
  }

  send(
    type: number,
    payload: Uint8Array,
    delivery: Delivery = "reliable",
    deadline = 0,
  ): void {
    if (!this.isReady())
      throw new Error("QUIC security readiness is incomplete");
    if (
      type === READY ||
      type === KEYFRAME_REQUEST ||
      type === ENCRYPTED ||
      (type >= 1112 && type <= 1114) ||
      (type >= 1201 && type <= 1203) ||
      (handshakeType(type) && type !== 2003)
    )
      throw new Error("Reserved QUIC packet type");
    const flow = flowOf(type);
    if (delivery === "datagram" && flow === 0)
      throw new Error("Control packets require reliable delivery");
    this.tick();
    const packet = this.callbacks.serialize(type, payload);
    const wire = this.security.seal(flow, delivery, packet);
    try {
      this.queue(flow, delivery, wire, deadline, type);
    } catch (error) {
      if (delivery === "reliable") throw error;
      this.reportDatagramDrop(type, error);
    }
  }

  receive(input: WebTransportPacket): void {
    if (this.closed) return;
    if (
      input.delivery === "datagram" &&
      (!this.securityActive || !this.peerReady)
    )
      return;
    try {
      let outerType: number;
      try {
        outerType = typeOf(input.packet);
      } catch (error) {
        if (input.delivery === "datagram") return;
        throw error;
      }
      if (!this.securityActive) {
        if (input.flow !== 0 || input.delivery !== "reliable")
          throw new Error("Media before QUIC authentication");
        if (outerType === ENCRYPTED) {
          if (this.early || input.packet.length > 256)
            throw new Error("Invalid early QUIC readiness packet");
          this.early = input.packet.slice();
          return;
        }
        if (!handshakeType(outerType))
          throw new Error("Unexpected plaintext QUIC handshake packet");
        this.callbacks.handshake(input.packet);
        return;
      }
      if (outerType !== ENCRYPTED) {
        if (input.delivery === "datagram") return;
        throw new Error("Plaintext packet after QUIC authentication");
      }
      let opened: { packet: Uint8Array; sequence: bigint };
      try {
        opened = this.security.open(input.flow, input.delivery, input.packet);
      } catch (error) {
        // Datagram authentication and replay failures are packet loss; a
        // reliable stream failure invalidates the session.
        if (input.delivery === "datagram") return;
        throw error;
      }
      if (this.acknowledged) this.tick();
      if (!opened.packet.length) return; // C consumed an authenticated coordinator message.
      const type = typeOf(opened.packet);
      if (type === READY) {
        if (
          input.flow !== 0 ||
          input.delivery !== "reliable" ||
          opened.packet.length !== HEADER + READY_BYTES.length
        )
          throw new Error("Invalid QUIC readiness packet");
        const payload = opened.packet.subarray(HEADER);
        if (!this.peerReady && same(payload, READY_BYTES)) {
          this.peerReady = true;
          this.sendInternal(READY, ACK_BYTES);
        } else if (
          this.peerReady &&
          !this.acknowledged &&
          same(payload, ACK_BYTES)
        ) {
          this.acknowledged = true;
          this.tick();
          if (this.security.tick)
            this.rekeyTimer = setInterval(() => {
              try {
                this.tick();
              } catch (error) {
                this.fail(error);
              }
            }, 250);
          this.callbacks.ready();
        } else throw new Error("Unexpected QUIC readiness packet");
        return;
      }
      if (!this.peerReady) throw new Error("Missing QUIC readiness packet");
      if ((type >= 1112 && type <= 1114) || (type >= 1201 && type <= 1203))
        throw new Error("Unconsumed QUIC rekey packet");
      if (type === KEYFRAME_REQUEST) {
        if (
          input.flow !== 0 ||
          input.delivery !== "reliable" ||
          opened.packet.length !== HEADER + 6
        )
          throw new Error("Invalid QUIC keyframe request");
        const view = new DataView(
          opened.packet.buffer,
          opened.packet.byteOffset + HEADER,
          6,
        );
        const codec = view.getUint16(0);
        const stream = view.getUint32(2);
        if ((codec !== 3002 && codec !== 3003) || stream !== 0)
          throw new Error("Unsupported QUIC keyframe request");
        this.callbacks.keyframe(codec, stream);
        return;
      }
      if (
        input.flow === 2 &&
        input.delivery === "datagram" &&
        !this.acceptVisual(opened.packet, type, opened.sequence)
      )
        return;
      this.callbacks.packet({
        flow: input.flow,
        delivery: input.delivery,
        packet: opened.packet,
      });
    } catch (error) {
      this.fail(error);
    }
  }

  close(): void {
    if (this.rekeyTimer !== undefined) clearInterval(this.rekeyTimer);
    this.rekeyTimer = undefined;
    this.closed = true;
    this.early = null;
  }

  private tick(): void {
    for (const output of this.security.tick?.() ?? [])
      this.queue(output.flow, "reliable", output.packet);
  }

  private acceptVisual(
    packet: Uint8Array,
    type: number,
    sequence: bigint,
  ): boolean {
    if (sequence <= this.lastVisual) return false;
    const gap = this.lastVisual !== 0n && sequence !== this.lastVisual + 1n;
    this.lastVisual = sequence;
    if (type !== 3002 && type !== 3003) {
      this.visualCodec = 0;
      this.waitingKeyframe = false;
      this.requestedKeyframe = false;
      return true;
    }
    if (gap || this.visualCodec !== type) this.waitingKeyframe = true;
    this.visualCodec = type;
    if ((packet[HEADER] ?? 0) & 1) {
      this.waitingKeyframe = false;
      this.requestedKeyframe = false;
    }
    if (!this.waitingKeyframe) return true;
    if (!this.requestedKeyframe && this.acknowledged) {
      const stream = new DataView(
        packet.buffer,
        packet.byteOffset,
        packet.byteLength,
      ).getUint32(18);
      if (stream === 0) {
        const payload = new Uint8Array(6);
        const view = new DataView(payload.buffer);
        view.setUint16(0, type);
        view.setUint32(2, stream);
        this.sendInternal(KEYFRAME_REQUEST, payload);
        this.requestedKeyframe = true;
      }
    }
    return false;
  }

  private sendInternal(type: number, payload: Uint8Array): void {
    const packet = this.callbacks.serialize(type, payload);
    this.queue(0, "reliable", this.security.seal(0, "reliable", packet));
  }

  private queue(
    flow: AcipFlow,
    delivery: Delivery,
    packet: Uint8Array,
    deadline = 0,
    type = 0,
  ): void {
    const pending = this.callbacks.send(flow, delivery, packet, deadline);
    void pending.catch((error: unknown) => {
      if (delivery === "reliable") {
        this.fail(error);
      } else {
        this.reportDatagramDrop(type, error);
      }
    });
  }

  private reportDatagramDrop(type: number, error: unknown): void {
    if (this.closed) return;
    if (type === 3002 || type === 3003) this.callbacks.keyframe(type, 0);
    this.callbacks.datagramDropped?.(
      type,
      error instanceof Error ? error : new Error(String(error)),
    );
  }

  private fail(error: unknown): void {
    if (this.closed) return;
    this.close();
    this.callbacks.failed(
      error instanceof Error ? error : new Error(String(error)),
    );
  }
}
