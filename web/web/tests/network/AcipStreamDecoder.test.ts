import { describe, expect, it } from "vite-plus/test";
import { AcipStreamDecoder } from "../../src/network/AcipStreamDecoder";
import { AcipQuicStreamDecoder } from "../../src/network/AcipQuicStreamDecoder";

function packet(type = 4000): Uint8Array {
  const bytes = new Uint8Array(23);
  const header = new DataView(bytes.buffer);
  header.setBigUint64(0, 0xa5c11c4a1n);
  header.setUint16(8, type);
  header.setUint32(10, 1);
  header.setUint32(14, 0xb048173d);
  bytes[22] = 17;
  return bytes;
}

describe("incremental ACIP streams", () => {
  it("assembles arbitrary splits and multiple packets", () => {
    const source = packet();
    for (let split = 0; split <= source.length; ++split) {
      const decoder = new AcipStreamDecoder();
      const output: Uint8Array[] = [];
      decoder.feed(source.subarray(0, split), (p) => output.push(p));
      decoder.feed(source.subarray(split), (p) => output.push(p));
      decoder.feed(source, (p) => output.push(p));
      decoder.finish();
      expect(output).toEqual([source, source]);
    }
  });

  it("rejects oversized, malformed, corrupted and truncated packets", () => {
    const invalid = packet();
    new DataView(invalid.buffer).setUint32(10, 0xffffffff);
    const decoder = new AcipStreamDecoder();
    expect(() => decoder.feed(invalid, () => {})).toThrow("header");
    expect(() => decoder.feed(packet(), () => {})).toThrow("closed");
    decoder.reset();
    const corrupted = packet();
    corrupted[22] = 18;
    expect(() => decoder.feed(corrupted, () => {})).toThrow("checksum");
    decoder.reset();
    const badMagic = packet();
    badMagic[0] = 1;
    expect(() => decoder.feed(badMagic, () => {})).toThrow("header");
    decoder.reset();
    decoder.feed(packet().subarray(0, 21), () => {});
    expect(() => decoder.finish()).toThrow("Truncated");
    decoder.reset();
    decoder.feed(packet().subarray(0, 22), () => {});
    expect(() => decoder.finish()).toThrow("Truncated");
  });

  it("validates QUIC stream preambles and plaintext traffic classes", () => {
    const input = new Uint8Array([65, 67, 81, 83, 1, 1, 0, 0, ...packet()]);
    const decoder = new AcipQuicStreamDecoder();
    const output: number[] = [];
    for (const byte of input)
      decoder.feed(new Uint8Array([byte]), (flow) => output.push(flow));
    decoder.finish();
    expect(output).toEqual([1]);
    const wrongFlow = new AcipQuicStreamDecoder();
    expect(() =>
      wrongFlow.feed(
        new Uint8Array([65, 67, 81, 83, 1, 0, 0, 0, ...packet()]),
        () => {},
      ),
    ).toThrow("wrong flow");
    const encrypted = new AcipQuicStreamDecoder();
    encrypted.feed(
      new Uint8Array([65, 67, 81, 83, 1, 2, 0, 0, ...packet(1200)]),
      () => {},
    );
    encrypted.finish();
    const invalid = new AcipQuicStreamDecoder();
    expect(() =>
      invalid.feed(new Uint8Array([65, 67, 81, 83, 1, 3, 0, 0]), () => {}),
    ).toThrow("preamble");
    const truncated = new AcipQuicStreamDecoder();
    truncated.feed(new Uint8Array([65, 67, 81]), () => {});
    expect(() => truncated.finish()).toThrow("Truncated");
  });
});
