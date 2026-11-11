import { describe, expect, it } from "vite-plus/test";
import {
  AcipQuicDatagramDecoder,
  encodeAcipQuicDatagram,
} from "../../src/network/AcipQuicDatagramDecoder";

function fragment(
  flow: 1 | 2,
  id: bigint,
  packet: Uint8Array,
  unit: number,
  index: number,
): Uint8Array {
  const payload = packet.subarray(
    index * unit,
    Math.min((index + 1) * unit, packet.length),
  );
  const bytes = new Uint8Array(28 + payload.length);
  bytes.set([65, 67, 81, 68, 1, flow]);
  const view = new DataView(bytes.buffer);
  view.setBigUint64(8, id);
  view.setUint32(16, packet.length);
  view.setUint16(20, unit);
  view.setUint16(22, index);
  view.setUint16(24, Math.ceil(packet.length / unit));
  bytes.set(payload, 28);
  return bytes;
}

describe("ACIP QUIC datagrams", () => {
  it("encodes the native ACQD wire format and validates fragment bounds", () => {
    const packet = Uint8Array.from([1, 2, 3, 4, 5]);
    for (let index = 0; index < 3; index++)
      expect(encodeAcipQuicDatagram(2, 99n, packet, 2, index)).toEqual(
        fragment(2, 99n, packet, 2, index),
      );
    expect(() => encodeAcipQuicDatagram(1, 0n, packet, 2, 0)).toThrow();
    expect(() => encodeAcipQuicDatagram(1, 1n, packet, 2, 3)).toThrow();
    expect(() => encodeAcipQuicDatagram(1, 1n, packet, 0, 0)).toThrow();
  });
  it("reassembles interleaved flows and reordered fragments, ignoring duplicates", () => {
    const reader = new AcipQuicDatagramDecoder();
    const media = Uint8Array.from([1, 2, 3, 4, 5]);
    const last = fragment(2, 99n, media, 2, 2);
    expect(reader.feed(last, 1)).toBeNull();
    expect(reader.feed(last, 2)).toBeNull();
    const audio = Uint8Array.from([8, 9]);
    expect(reader.feed(fragment(1, 99n, audio, 2, 0), 3)).toEqual({
      flow: 1,
      packet: audio,
    });
    expect(reader.feed(fragment(2, 99n, media, 2, 0), 4)).toBeNull();
    expect(reader.feed(fragment(2, 99n, media, 2, 1), 5)).toEqual({
      flow: 2,
      packet: media,
    });
  });
  it("rejects malformed and conflicting fragments", () => {
    const reader = new AcipQuicDatagramDecoder();
    const bytes = fragment(1, 1n, Uint8Array.from([1, 2, 3]), 2, 0);
    expect(reader.feed(bytes, 0)).toBeNull();
    const conflict = bytes.slice();
    conflict[28] = 9;
    expect(() => reader.feed(conflict, 1)).toThrow(/Conflicting duplicate/);
    const malformed = bytes.slice();
    malformed[6] = 1;
    expect(() => reader.feed(malformed, 2)).toThrow(/envelope/);
    expect(() => reader.feed(bytes.subarray(0, bytes.length - 1), 3)).toThrow(
      /size/,
    );
    expect(() => reader.feed(bytes, -1)).toThrow(/clock|input/);
  });
  it("enforces the byte budget independently for each media flow", () => {
    const reader = new AcipQuicDatagramDecoder();
    const packet = new Uint8Array(1024 * 1024 + 62);
    expect(reader.feed(fragment(2, 1n, packet, 1024, 0), 0)).toBeNull();
    expect(() => reader.feed(fragment(2, 2n, packet, 1024, 0), 1)).toThrow(
      /budget/,
    );
    expect(reader.feed(fragment(1, 2n, packet, 1024, 0), 1)).toBeNull();
    reader.reset();
    expect(reader.feed(fragment(2, 2n, packet, 1024, 0), 0)).toBeNull();
  });

  it("bounds incomplete packets per flow and expires them independently", () => {
    const reader = new AcipQuicDatagramDecoder();
    const packet = Uint8Array.from([1, 2]);
    for (let id = 1n; id <= 4n; id++)
      expect(reader.feed(fragment(2, id, packet, 1, 0), 1)).toBeNull();
    expect(() => reader.feed(fragment(2, 5n, packet, 1, 0), 2)).toThrow(
      /budget/,
    );
    expect(reader.feed(fragment(1, 5n, packet, 1, 0), 2)).toBeNull();
    reader.expire(1001);
    expect(reader.feed(fragment(2, 5n, packet, 1, 0), 1001)).toBeNull();
    expect(reader.feed(fragment(2, 5n, packet, 1, 1), 1002)).toEqual({
      flow: 2,
      packet,
    });
  });
});
