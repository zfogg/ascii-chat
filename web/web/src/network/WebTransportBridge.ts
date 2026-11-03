import { AcipQuicStreamDecoder, type AcipFlow } from "./AcipQuicStreamDecoder";
import {
  AcipQuicDatagramDecoder,
  encodeAcipQuicDatagram,
} from "./AcipQuicDatagramDecoder";

/** The packets passed across this boundary are complete ACIP wire packets.
 * After the handshake the caller must seal outgoing packets and authenticate
 * incoming packets against the supplied flow and delivery before dispatch. */
export interface WebTransportSessionLike {
  readonly ready: Promise<void>;
  readonly closed: Promise<unknown>;
  readonly incomingUnidirectionalStreams: ReadableStream<
    ReadableStream<Uint8Array>
  >;
  readonly datagrams: {
    readonly readable: ReadableStream<Uint8Array>;
    readonly writable: WritableStream<Uint8Array>;
    readonly maxDatagramSize: number;
  };
  createUnidirectionalStream(): Promise<WritableStream<Uint8Array>>;
  close(): void;
}

export type WebTransportPacket = {
  flow: AcipFlow;
  delivery: "reliable" | "datagram";
  packet: Uint8Array;
};

const BUDGET = 8 * 1024 * 1024;
const CONTROL_RESERVE = 512 * 1024;
const AUDIO_RESERVE = 512 * 1024;
const HEADER = 28;

/** Packet/flow mapping for one established WebTransport session. The session
 * owner is responsible for ACIP authentication, readiness and key changes. */
export class WebTransportBridge {
  private readonly decoder = new AcipQuicDatagramDecoder();
  private readonly writers: Array<
    WritableStreamDefaultWriter<Uint8Array> | undefined
  > = [];
  private readonly received = new Set<AcipFlow>();
  private readonly tails: Promise<void>[] = [
    Promise.resolve(),
    Promise.resolve(),
    Promise.resolve(),
  ];
  private datagramWriter: WritableStreamDefaultWriter<Uint8Array> | undefined;
  private readonly datagramTails: Promise<void>[] = [
    Promise.resolve(),
    Promise.resolve(),
    Promise.resolve(),
  ];
  private packetId = [0n, 0n, 0n];
  private buffered = 0;
  private incomingCount = 0;
  private state: "new" | "connecting" | "open" | "closed" = "new";
  private stopped = false;
  private expiryTimer: ReturnType<typeof setInterval> | undefined;

  constructor(
    private readonly session: WebTransportSessionLike,
    private readonly onPacket: (packet: WebTransportPacket) => void,
    private readonly onError: (error: Error) => void,
    private readonly now: () => number = () => performance.now(),
  ) {}

  async connect(): Promise<void> {
    if (this.state !== "new")
      throw new Error("WebTransport session already started");
    this.state = "connecting";
    try {
      await this.session.ready;
      if (this.stopped) throw new Error("WebTransport session closed");
      for (const flow of [0, 1, 2] as const) {
        const stream = await this.session.createUnidirectionalStream();
        if (this.stopped) throw new Error("WebTransport session closed");
        const writer = stream.getWriter();
        this.writers[flow] = writer;
        await writer.write(new Uint8Array([65, 67, 81, 83, 1, flow, 0, 0]));
      }
      this.datagramWriter = this.session.datagrams.writable.getWriter();
      this.state = "open";
      this.expiryTimer = setInterval(
        () => this.decoder.expire(this.now()),
        250,
      );
      void this.acceptStreams().catch((error: unknown) => this.fail(error));
      void this.acceptDatagrams().catch((error: unknown) => this.fail(error));
      void this.session.closed.then(
        () => this.fail(new Error("WebTransport session closed")),
        (error: unknown) => this.fail(error),
      );
    } catch (error) {
      this.close();
      throw error;
    }
  }

  isConnected(): boolean {
    return this.state === "open";
  }
  getBufferedAmount(): number {
    return this.buffered;
  }

  /** No reliable fallback occurs after datagram admission. deadlineMs uses the
   * same monotonic clock as performance.now(), and zero disables expiry. */
  send(
    flow: AcipFlow,
    delivery: "reliable" | "datagram",
    packet: Uint8Array,
    deadlineMs = 0,
  ): Promise<void> {
    if (!this.isConnected())
      throw new Error("WebTransport session is not connected");
    if (
      flow < 0 ||
      flow > 2 ||
      !Number.isInteger(flow) ||
      (delivery !== "reliable" && delivery !== "datagram")
    )
      throw new Error("Invalid WebTransport delivery metadata");
    if (
      packet.length < 22 ||
      packet.length > 1024 * 1024 + 62 ||
      !Number.isFinite(deadlineMs) ||
      deadlineMs < 0
    )
      throw new Error("Invalid WebTransport packet or deadline");
    if (
      delivery === "datagram" &&
      (flow === 0 ||
        new DataView(
          packet.buffer,
          packet.byteOffset,
          packet.byteLength,
        ).getUint16(8) !== 1200)
    )
      throw new Error("WebTransport datagrams require encrypted media packets");
    if (delivery === "reliable" && deadlineMs !== 0)
      throw new Error("Reliable delivery does not support deadlines");
    const limit =
      flow === 2
        ? BUDGET - CONTROL_RESERVE - AUDIO_RESERVE
        : flow === 1
          ? BUDGET - CONTROL_RESERVE
          : BUDGET;
    // Account for each queued promise and descriptor as well as packet bytes.
    const cost = packet.length + 256;
    if (cost > limit - this.buffered)
      throw new Error("WebTransport send budget exhausted");
    if (deadlineMs && this.now() >= deadlineMs)
      throw new Error("WebTransport datagram deadline expired");
    const copy = packet.slice();
    this.buffered += cost;
    const send = async () => {
      if (!this.isConnected()) throw new Error("WebTransport session closed");
      if (deadlineMs && this.now() >= deadlineMs)
        throw new Error("WebTransport datagram deadline expired");
      if (delivery === "reliable") {
        await this.writers[flow]!.write(copy);
      } else {
        const maximum = this.session.datagrams.maxDatagramSize;
        const unit = Math.min(65535, Math.floor(maximum) - HEADER);
        if (!Number.isFinite(maximum) || unit <= 0)
          throw new Error("WebTransport datagrams unavailable");
        const id = this.packetId[flow]! + 1n;
        if (id > 0xffffffffffffffffn)
          throw new Error("WebTransport datagram sequence exhausted");
        this.packetId[flow] = id;
        const count = Math.ceil(copy.length / unit);
        if (count > 4096)
          throw new Error("WebTransport packet requires too many fragments");
        for (let index = 0; index < count; ++index) {
          if (deadlineMs && this.now() >= deadlineMs)
            throw new Error("WebTransport datagram deadline expired");
          if (
            this.session.datagrams.maxDatagramSize <
            HEADER + Math.min(unit, copy.length - index * unit)
          )
            throw new Error("WebTransport datagram size changed");
          await this.datagramWriter!.write(
            encodeAcipQuicDatagram(flow as 1 | 2, id, copy, unit, index),
          );
        }
      }
    };
    const previous =
      delivery === "datagram" ? this.datagramTails[flow]! : this.tails[flow]!;
    const operation = previous.then(send).finally(() => {
      this.buffered -= cost;
    });
    const settled = operation.catch(() => {});
    if (delivery === "datagram") this.datagramTails[flow] = settled;
    else this.tails[flow] = settled;
    return operation;
  }

  close(): void {
    if (this.stopped) return;
    this.stopped = true;
    this.state = "closed";
    if (this.expiryTimer !== undefined) clearInterval(this.expiryTimer);
    this.decoder.reset();
    this.session.close();
  }

  private fail(error: unknown): void {
    if (this.stopped) return;
    this.close();
    this.onError(error instanceof Error ? error : new Error(String(error)));
  }

  private async acceptStreams(): Promise<void> {
    const reader = this.session.incomingUnidirectionalStreams.getReader();
    try {
      while (!this.stopped) {
        const next = await reader.read();
        if (next.done) throw new Error("WebTransport incoming streams closed");
        if (++this.incomingCount > 3)
          throw new Error("Too many WebTransport ACIP streams");
        void this.readStream(next.value).catch((error: unknown) =>
          this.fail(error),
        );
      }
    } finally {
      reader.releaseLock();
    }
  }

  private async readStream(stream: ReadableStream<Uint8Array>): Promise<void> {
    const reader = stream.getReader();
    const decoder = new AcipQuicStreamDecoder();
    try {
      while (!this.stopped) {
        const next = await reader.read();
        if (next.done) {
          decoder.finish();
          throw new Error("WebTransport ACIP stream closed");
        }
        decoder.feed(
          next.value,
          (flow, packet) =>
            this.onPacket({ flow, delivery: "reliable", packet }),
          (flow) => {
            if (this.received.has(flow))
              throw new Error("Duplicate WebTransport ACIP flow");
            this.received.add(flow);
          },
        );
      }
    } finally {
      reader.releaseLock();
    }
  }

  private async acceptDatagrams(): Promise<void> {
    const reader = this.session.datagrams.readable.getReader();
    try {
      while (!this.stopped) {
        const next = await reader.read();
        if (next.done) throw new Error("WebTransport datagram reader closed");
        let result: ReturnType<AcipQuicDatagramDecoder["feed"]>;
        try {
          result = this.decoder.feed(next.value, this.now());
        } catch {
          continue;
        }
        if (result)
          this.onPacket({
            flow: result.flow,
            delivery: "datagram",
            packet: result.packet,
          });
      }
    } finally {
      reader.releaseLock();
    }
  }
}

/** Create a browser session using the browser's certificate and origin policy.
 * Callers decide whether a failed connection should use another transport. */
export function createBrowserWebTransportBridge(
  url: string,
  onPacket: (packet: WebTransportPacket) => void,
  onError: (error: Error) => void,
  options?: WebTransportOptions,
): WebTransportBridge {
  const endpoint = new URL(url);
  if (endpoint.protocol !== "https:")
    throw new Error("WebTransport requires an HTTPS URL");
  if (typeof WebTransport === "undefined")
    throw new Error("WebTransport is not available in this browser");
  return new WebTransportBridge(
    new WebTransport(endpoint.href, options),
    onPacket,
    onError,
  );
}
