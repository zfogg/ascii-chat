import { describe, expect, it, vi } from "vite-plus/test";
import { AcipQuicProtocol } from "../../src/network/AcipQuicProtocol";
import type { AcipFlow } from "../../src/network/AcipQuicStreamDecoder";
import type { WebTransportPacket } from "../../src/network/WebTransportBridge";

type Delivery = "reliable" | "datagram";
function packet(
  type: number,
  payload: Uint8Array = new Uint8Array(),
): Uint8Array {
  const value = new Uint8Array(22 + payload.length);
  const view = new DataView(value.buffer);
  view.setUint16(8, type);
  view.setUint32(10, payload.length);
  value.set(payload, 22);
  return value;
}
function wrap(inner: Uint8Array, sequence = 1n): Uint8Array {
  const outer = packet(1200, new Uint8Array(8 + inner.length));
  new DataView(outer.buffer).setBigUint64(22, sequence);
  outer.set(inner, 30);
  return outer;
}
const ready = packet(1110, new Uint8Array([65, 67, 81, 82, 1]));
const ack = packet(1110, new Uint8Array([65, 67, 81, 65, 1]));
function fixture(tick?: () => { flow: AcipFlow; packet: Uint8Array }[]) {
  const sent: WebTransportPacket[] = [],
    received: WebTransportPacket[] = [],
    handshakes: Uint8Array[] = [];
  const readyCallback = vi.fn(),
    failed = vi.fn(),
    keyframe = vi.fn(),
    dropped = vi.fn();
  const security = {
    ...(tick ? { tick } : {}),
    init: vi.fn(),
    seal: vi.fn((_flow: AcipFlow, _delivery: Delivery, value: Uint8Array) =>
      wrap(value),
    ),
    open: vi.fn((_flow: AcipFlow, _delivery: Delivery, value: Uint8Array) => ({
      packet: value.slice(30),
      sequence: new DataView(value.buffer, value.byteOffset).getBigUint64(22),
    })),
  };
  let rejectDatagram = false,
    throwDatagram = false;
  const callbacks = {
    send: vi.fn((flow: AcipFlow, delivery: Delivery, value: Uint8Array) => {
      sent.push({ flow, delivery, packet: value });
      if (throwDatagram && delivery === "datagram")
        throw new Error("congested");
      return rejectDatagram && delivery === "datagram"
        ? Promise.reject(new Error("expired"))
        : Promise.resolve();
    }),
    serialize: packet,
    handshake: (value: Uint8Array) => handshakes.push(value),
    packet: (value: WebTransportPacket) => received.push(value),
    ready: readyCallback,
    keyframe,
    datagramDropped: dropped,
    failed,
  };
  const protocol = new AcipQuicProtocol(security, callbacks);
  const receive = (
    value: Uint8Array,
    flow: AcipFlow = 0,
    delivery: Delivery = "reliable",
    sequence = 1n,
  ) => protocol.receive({ flow, delivery, packet: wrap(value, sequence) });
  const establish = () => {
    protocol.authenticated();
    receive(ready);
    receive(ack);
  };
  return {
    protocol,
    sent,
    received,
    handshakes,
    readyCallback,
    failed,
    keyframe,
    dropped,
    security,
    receive,
    establish,
    rejectDatagrams: () => {
      rejectDatagram = true;
    },
    throwDatagrams: () => {
      throwDatagram = true;
    },
  };
}

describe("QUIC authenticated readiness", () => {
  it("buffers an early peer READY and sends media only after peer acknowledgement", () => {
    const f = fixture();
    f.protocol.sendHandshake(packet(1));
    f.protocol.receive({ flow: 0, delivery: "reliable", packet: packet(1101) });
    expect(f.handshakes).toHaveLength(1);
    f.receive(ready);
    expect(() => f.protocol.send(4000, new Uint8Array())).toThrow(/readiness/);
    f.protocol.authenticated();
    expect(
      f.sent.map((item) => new DataView(item.packet.buffer).getUint16(8)),
    ).toEqual([1, 1200, 1200]);
    expect(f.sent[1]!.packet.slice(30)).toEqual(ready);
    expect(f.sent[2]!.packet.slice(30)).toEqual(ack);
    f.receive(ack);
    expect(f.readyCallback).toHaveBeenCalledOnce();
    f.protocol.send(4000, new Uint8Array([3]), "datagram", 100);
    expect(f.sent.at(-1)).toMatchObject({ flow: 1, delivery: "datagram" });
    expect(f.failed).not.toHaveBeenCalled();
  });
  it("rejects unexpected plaintext and invalid acknowledgement ordering", () => {
    const f = fixture();
    f.protocol.authenticated();
    f.protocol.receive({ flow: 0, delivery: "reliable", packet: packet(2003) });
    expect(f.failed).toHaveBeenCalledOnce();
    expect(f.protocol.isReady()).toBe(false);
    const other = fixture();
    other.protocol.authenticated();
    other.receive(ack);
    expect(other.failed).toHaveBeenCalledOnce();
  });
  it("suppresses stale and broken predictive video and requests a new keyframe", () => {
    const f = fixture();
    f.establish();
    f.receive(packet(3002, new Uint8Array([1])), 2, "datagram", 1n);
    f.receive(packet(3002, new Uint8Array([0])), 2, "datagram", 3n);
    f.receive(packet(3002, new Uint8Array([0])), 2, "datagram", 2n);
    expect(f.received).toHaveLength(1);
    const request = f.sent.at(-1)!.packet.slice(30);
    expect(new DataView(request.buffer, request.byteOffset).getUint16(8)).toBe(
      1111,
    );
    expect(new DataView(request.buffer, request.byteOffset).getUint16(22)).toBe(
      3002,
    );
    f.receive(packet(3002, new Uint8Array([1])), 2, "datagram", 4n);
    f.receive(packet(3002, new Uint8Array([0])), 2, "datagram", 5n);
    expect(f.received).toHaveLength(3);
    expect(f.failed).not.toHaveBeenCalled();
  });
  it("ignores unauthenticated or malformed datagrams before and after readiness", () => {
    const f = fixture();
    f.protocol.receive({
      flow: 2,
      delivery: "datagram",
      packet: new Uint8Array([1]),
    });
    f.establish();
    f.protocol.receive({
      flow: 2,
      delivery: "datagram",
      packet: new Uint8Array([1]),
    });
    f.protocol.receive({ flow: 2, delivery: "datagram", packet: packet(3002) });
    expect(f.failed).not.toHaveBeenCalled();
    expect(f.protocol.isReady()).toBe(true);
  });
  it("drops unauthenticated datagrams without closing the reliable session", () => {
    const f = fixture();
    f.establish();
    f.security.open.mockImplementationOnce(() => {
      throw new Error("replay");
    });
    f.receive(packet(4000, new Uint8Array([1])), 1, "datagram");
    expect(f.received).toHaveLength(0);
    expect(f.failed).not.toHaveBeenCalled();
    expect(f.protocol.isReady()).toBe(true);
    f.security.open.mockImplementationOnce(() => {
      throw new Error("invalid stream packet");
    });
    f.receive(packet(4000, new Uint8Array([1])), 1, "reliable");
    expect(f.failed).toHaveBeenCalledOnce();
  });
  it("validates authenticated keyframe requests before notifying the encoder", () => {
    const f = fixture();
    f.establish();
    f.receive(packet(1111, Uint8Array.from([0x0b, 0xbb, 0, 0, 0, 0])));
    expect(f.keyframe).toHaveBeenCalledWith(3003, 0);
    f.receive(packet(1111, Uint8Array.from([0x0b, 0xbb, 0, 0, 0, 1])));
    expect(f.failed).toHaveBeenCalledOnce();
    expect(f.keyframe).toHaveBeenCalledOnce();
  });
  it("treats synchronous datagram congestion as a recoverable frame drop", () => {
    const f = fixture();
    f.establish();
    f.throwDatagrams();
    f.protocol.send(3002, new Uint8Array([1]), "datagram");
    expect(f.dropped).toHaveBeenCalledOnce();
    expect(f.keyframe).toHaveBeenCalledWith(3002, 0);
    expect(f.failed).not.toHaveBeenCalled();
  });
  it("notifies the encoder when a datagram is dropped without closing the session", async () => {
    const f = fixture();
    f.establish();
    f.rejectDatagrams();
    f.protocol.send(3003, new Uint8Array([1]), "datagram");
    await vi.waitFor(() => expect(f.dropped).toHaveBeenCalledOnce());
    expect(f.keyframe).toHaveBeenCalledWith(3003, 0);
    expect(f.failed).not.toHaveBeenCalled();
    expect(f.protocol.isReady()).toBe(true);
  });
});

describe("shared C rekey integration", () => {
  it("drains reliable retirement markers before later application media", () => {
    vi.useFakeTimers();
    const tick = vi
      .fn<() => { flow: AcipFlow; packet: Uint8Array }[]>()
      .mockReturnValue([]);
    const f = fixture(tick);
    try {
      f.establish();
      const marker = wrap(packet(1112));
      tick.mockReturnValueOnce([{ flow: 2, packet: marker }]);
      const before = f.sent.length;
      f.protocol.send(3001, new Uint8Array([1]));
      expect(f.sent[before]).toEqual({
        flow: 2,
        delivery: "reliable",
        packet: marker,
      });
      expect(f.sent[before + 1]?.flow).toBe(2);
      expect(f.failed).not.toHaveBeenCalled();
    } finally {
      f.protocol.close();
      vi.useRealTimers();
    }
  });

  it("consumes internal messages and cancels rekey ticks before crypto cleanup", () => {
    vi.useFakeTimers();
    const tick = vi
      .fn<() => { flow: AcipFlow; packet: Uint8Array }[]>()
      .mockReturnValue([]);
    const f = fixture(tick);
    try {
      f.establish();
      f.security.open.mockReturnValueOnce({
        packet: new Uint8Array(),
        sequence: 2n,
      });
      f.receive(packet(1114));
      expect(f.received).toHaveLength(0);
      expect(f.failed).not.toHaveBeenCalled();
      vi.advanceTimersByTime(250);
      const count = tick.mock.calls.length;
      f.protocol.close();
      vi.advanceTimersByTime(1000);
      expect(tick).toHaveBeenCalledTimes(count);
    } finally {
      f.protocol.close();
      vi.useRealTimers();
    }
  });
});
