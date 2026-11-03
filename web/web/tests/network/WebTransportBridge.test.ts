import { describe, expect, it, vi } from "vite-plus/test";
import {
  WebTransportBridge,
  createBrowserWebTransportBridge,
  type WebTransportPacket,
  type WebTransportSessionLike,
} from "../../src/network/WebTransportBridge";
import { encodeAcipQuicDatagram } from "../../src/network/AcipQuicDatagramDecoder";

function packet(type: number): Uint8Array {
  const bytes = new Uint8Array(23);
  const view = new DataView(bytes.buffer);
  view.setBigUint64(0, 0xa5c11c4a1n);
  view.setUint16(8, type);
  view.setUint32(10, 1);
  view.setUint32(14, 0xb8b2cf7f);
  bytes[22] = 17;
  return bytes;
}

function sessionFixture(
  visualGate?: Promise<void>,
  datagramGate?: Promise<void>,
) {
  let incoming!: ReadableStreamDefaultController<ReadableStream<Uint8Array>>;
  let datagrams!: ReadableStreamDefaultController<Uint8Array>;
  const streams: Uint8Array[][] = [];
  const sentDatagrams: Uint8Array[] = [];
  const close = vi.fn();
  const session: WebTransportSessionLike = {
    ready: Promise.resolve(),
    closed: new Promise(() => {}),
    incomingUnidirectionalStreams: new ReadableStream({
      start(controller) {
        incoming = controller;
      },
    }),
    datagrams: {
      readable: new ReadableStream({
        start(controller) {
          datagrams = controller;
        },
      }),
      writable: new WritableStream({
        async write(value) {
          sentDatagrams.push(value.slice());
          if (sentDatagrams.length === 1) await datagramGate;
        },
      }),
      maxDatagramSize: 40,
    },
    async createUnidirectionalStream() {
      const chunks: Uint8Array[] = [];
      streams.push(chunks);
      const flow = streams.length - 1;
      return new WritableStream<Uint8Array>({
        async write(value) {
          chunks.push(value.slice());
          if (flow === 2 && chunks.length > 1) await visualGate;
        },
      });
    },
    close,
  };
  function incomingStream(...chunks: Uint8Array[]) {
    const stream = new ReadableStream<Uint8Array>({
      start(controller) {
        for (const chunk of chunks) controller.enqueue(chunk);
      },
    });
    incoming.enqueue(stream);
  }
  return { session, streams, sentDatagrams, datagrams, incomingStream, close };
}

describe("WebTransport ACIP bridge", () => {
  it("requires an HTTPS endpoint and reports unavailable browser support", () => {
    expect(() =>
      createBrowserWebTransportBridge(
        "http://localhost:1234/",
        () => {},
        () => {},
      ),
    ).toThrow(/HTTPS/);
    vi.stubGlobal("WebTransport", undefined);
    try {
      expect(() =>
        createBrowserWebTransportBridge(
          "https://localhost:1234/",
          () => {},
          () => {},
        ),
      ).toThrow(/not available/);
    } finally {
      vi.unstubAllGlobals();
    }
  });
  it("maps independent reliable streams and bounded media datagrams", async () => {
    const fixture = sessionFixture();
    const packets: WebTransportPacket[] = [];
    const errors: Error[] = [];
    const bridge = new WebTransportBridge(
      fixture.session,
      (value) => packets.push(value),
      (error) => errors.push(error),
      () => 10,
    );
    await bridge.connect();
    expect(fixture.streams.map((chunks) => [...chunks[0]!])).toEqual(
      [0, 1, 2].map((flow) => [65, 67, 81, 83, 1, flow, 0, 0]),
    );
    const wire = packet(1200);
    await bridge.send(1, "reliable", wire);
    expect(fixture.streams[1]![1]).toEqual(wire);
    await bridge.send(2, "datagram", wire, 100);
    expect(fixture.sentDatagrams).toEqual([
      encodeAcipQuicDatagram(2, 1n, wire, 12, 0),
      encodeAcipQuicDatagram(2, 1n, wire, 12, 1),
    ]);
    expect(bridge.getBufferedAmount()).toBe(0);
    fixture.incomingStream(
      new Uint8Array([65, 67, 81]),
      new Uint8Array([83, 1, 1, 0, 0, ...wire]),
    );
    fixture.datagrams.enqueue(fixture.sentDatagrams[1]!);
    fixture.datagrams.enqueue(fixture.sentDatagrams[0]!);
    await vi.waitFor(() => expect(packets).toHaveLength(2));
    expect(packets).toContainEqual({
      flow: 1,
      delivery: "reliable",
      packet: wire,
    });
    expect(packets).toContainEqual({
      flow: 2,
      delivery: "datagram",
      packet: wire,
    });
    expect(errors).toEqual([]);
    bridge.close();
    expect(fixture.close).toHaveBeenCalledOnce();
  });
  it("interleaves audio with a fragmented visual datagram", async () => {
    let release!: () => void;
    const gate = new Promise<void>((resolve) => {
      release = resolve;
    });
    const fixture = sessionFixture(undefined, gate);
    const bridge = new WebTransportBridge(
      fixture.session,
      () => {},
      () => {},
    );
    await bridge.connect();
    const visual = bridge.send(2, "datagram", packet(1200));
    await vi.waitFor(() => expect(fixture.sentDatagrams).toHaveLength(1));
    const audio = bridge.send(1, "datagram", packet(1200));
    await Promise.resolve();
    release();
    await Promise.all([visual, audio]);
    const flows = fixture.sentDatagrams.map((item) => item[5]);
    expect(flows.indexOf(1)).toBeLessThan(flows.lastIndexOf(2));
    bridge.close();
  });
  it("reserves control capacity while visual writes are blocked", async () => {
    let release!: () => void;
    const gate = new Promise<void>((resolve) => {
      release = resolve;
    });
    const fixture = sessionFixture(gate);
    const bridge = new WebTransportBridge(
      fixture.session,
      () => {},
      () => {},
    );
    await bridge.connect();
    const visual = new Uint8Array(1024 * 1024);
    const pending = Array.from({ length: 6 }, () =>
      bridge.send(2, "reliable", visual),
    );
    expect(() => bridge.send(2, "reliable", visual)).toThrow(/budget/);
    await bridge.send(0, "reliable", packet(1200));
    expect(fixture.streams[0]).toHaveLength(2);
    release();
    await Promise.all(pending);
    expect(bridge.getBufferedAmount()).toBe(0);
    bridge.close();
  });
  it("rejects insecure datagrams, stale packets and duplicate incoming flows", async () => {
    const fixture = sessionFixture();
    const errors: Error[] = [];
    const bridge = new WebTransportBridge(
      fixture.session,
      () => {},
      (error) => errors.push(error),
      () => 10,
    );
    await bridge.connect();
    expect(() => bridge.send(0, "datagram", packet(1200))).toThrow(
      /encrypted media/,
    );
    expect(() => bridge.send(2, "datagram", packet(3000))).toThrow(
      /encrypted media/,
    );
    expect(() => bridge.send(1, "datagram", packet(1200), 10)).toThrow(
      /deadline/,
    );
    fixture.incomingStream(new Uint8Array([65, 67, 81, 83, 1, 1, 0, 0]));
    fixture.incomingStream(new Uint8Array([65, 67, 81, 83, 1, 1, 0, 0]));
    await vi.waitFor(() => expect(errors).toHaveLength(1));
    expect(errors[0]!.message).toMatch(/Duplicate/);
    expect(bridge.isConnected()).toBe(false);
  });
});
