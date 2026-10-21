/**
 * Unit tests for AsciiFrameParser
 */

import { describe, it, expect, vi } from "vite-plus/test";
import { decompress } from "fzstd";
import { validateReceivedFrame } from "../../src/wasm/client";

// These tests exercise wire framing only. Actual content policy is exercised
// against the shared C validator by the native corpus and real WASM/browser tests.
vi.mock("../../src/wasm/client", () => ({
  validateReceivedFrame: vi.fn((bytes: Uint8Array, _w: number, _h: number, _crc: number, flags: number, _metadata: Uint8Array, size: number) => {
    const output = flags & 2 ? decompress(bytes) : bytes;
    if (output.length !== size) throw new Error("Decompressed ASCII frame size mismatch");
    return output;
  }),
}));
import {
  parseAsciiFrame,
  ASCII_FRAME_HEADER_SIZE,
  FrameFlags,
} from "../../src/network/AsciiFrameParser";

function buildFramePayload(
  width: number,
  height: number,
  frameData: string,
  flags: number = FrameFlags.HAS_COLOR,
): Uint8Array {
  const encoder = new TextEncoder();
  const frameBytes = encoder.encode(frameData);
  const buf = new ArrayBuffer(ASCII_FRAME_HEADER_SIZE + frameBytes.length);
  const view = new DataView(buf);
  const bytes = new Uint8Array(buf);

  // Network byte order (big-endian) - matches C code using HOST_TO_NET_U32
  view.setUint32(0, width, false);
  view.setUint32(4, height, false);
  view.setUint32(8, frameBytes.length, false); // original_size
  view.setUint32(12, 0, false); // compressed_size (uncompressed)
  view.setUint32(16, 0, false); // checksum (not validated in parser)
  view.setUint32(20, flags, false);

  bytes.set(frameBytes, ASCII_FRAME_HEADER_SIZE);
  return bytes;
}

describe("AsciiFrameParser", () => {
  it("fails closed when the shared C content validator rejects", () => {
    vi.mocked(validateReceivedFrame).mockImplementationOnce(() => { throw new Error("Invalid received rendered frame"); });
    expect(() => parseAsciiFrame(buildFramePayload(4, 1, "abcd"))).toThrow("Invalid received rendered frame");
  });

  it("passes a bounded versioned contract to C without decoding its policy in JS", () => {
    const legacy = buildFramePayload(4, 1, "abcd", FrameFlags.RENDER_CONTRACT);
    const payload = new Uint8Array(legacy.length + 24);
    payload.set(legacy.subarray(0, 24));
    const view = new DataView(payload.buffer);
    view.setUint32(24, 1); // schema
    view.setUint32(28, 9); // revision
    view.setUint32(40, 4); // palette length
    payload.set(new TextEncoder().encode("abcd"), 44);
    payload.set(legacy.subarray(24), 48);
    expect(parseAsciiFrame(payload).ansiString).toBe("abcd");
    expect(vi.mocked(validateReceivedFrame).mock.lastCall?.[5]).toEqual(payload.subarray(24, 48));
    expect(() => parseAsciiFrame(payload.subarray(0, 46))).toThrow("Truncated frame contract palette");
  });

  it("should export header size constant", () => {
    expect(ASCII_FRAME_HEADER_SIZE).toBe(24);
  });

  it("should export flag constants", () => {
    expect(FrameFlags.HAS_COLOR).toBe(0x01);
    expect(FrameFlags.IS_COMPRESSED).toBe(0x02);
  });

  it("should parse a simple uncompressed frame", () => {
    const frameData = "Hello\nWorld";
    const payload = buildFramePayload(80, 24, frameData);

    const result = parseAsciiFrame(payload);
    expect(result.header.width).toBe(80);
    expect(result.header.height).toBe(24);
    expect(result.header.originalSize).toBe(
      new TextEncoder().encode(frameData).length,
    );
    expect(result.header.compressedSize).toBe(0);
    expect(result.header.flags).toBe(FrameFlags.HAS_COLOR);
    expect(result.ansiString).toBe(frameData);
  });

  it("should parse frame with ANSI escape codes", () => {
    const frameData = "\x1b[31mRed Text\x1b[0m\n\x1b[32mGreen\x1b[0m";
    const payload = buildFramePayload(40, 12, frameData);

    const result = parseAsciiFrame(payload);
    expect(result.ansiString).toBe(frameData);
    expect(result.ansiString).toContain("\x1b[31m");
    expect(result.ansiString).toContain("\x1b[32m");
  });

  it("should parse frame with no flags (monochrome)", () => {
    const frameData = "####\n....";
    const payload = buildFramePayload(4, 2, frameData, 0);

    const result = parseAsciiFrame(payload);
    expect(result.header.flags).toBe(0);
    expect(result.ansiString).toBe(frameData);
  });

  it("should parse frame with UTF-8 multibyte characters", () => {
    const frameData = "░░▒▒▓▓██";
    const payload = buildFramePayload(8, 1, frameData);

    const result = parseAsciiFrame(payload);
    expect(result.ansiString).toBe(frameData);
    // UTF-8 encoded size is larger than character count
    expect(result.header.originalSize).toBeGreaterThan(frameData.length);
  });

  it("should throw on payload shorter than header", () => {
    const tooShort = new Uint8Array(10);
    expect(() => parseAsciiFrame(tooShort)).toThrow(
      "ASCII frame payload too short",
    );
  });

  // A real Zstandard frame with a four-byte RLE block containing "XXXX".
  // Frame magic, single-segment descriptor, content size, last RLE block, value.
  function compressedPayload() {
    const compressed = new Uint8Array([
      0x28, 0xb5, 0x2f, 0xfd, 0x20, 4, 0x23, 0, 0, 0x58,
    ]);
    const payload = new Uint8Array(ASCII_FRAME_HEADER_SIZE + compressed.length);
    payload.set(
      buildFramePayload(4, 1, "XXXX", FrameFlags.IS_COMPRESSED).subarray(
        0,
        ASCII_FRAME_HEADER_SIZE,
      ),
    );
    new DataView(payload.buffer).setUint32(12, compressed.length, false);
    payload.set(compressed, ASCII_FRAME_HEADER_SIZE);
    return payload;
  }

  it("decodes a Zstandard frame from an offset buffer", () => {
    const payload = compressedPayload();
    const outer = new Uint8Array(payload.length + 7);
    outer.set(payload, 7);
    const result = parseAsciiFrame(outer.subarray(7));
    expect(result.ansiString).toBe("XXXX");
    expect(result.header).toMatchObject({
      width: 4,
      height: 1,
      originalSize: 4,
      compressedSize: 10,
    });
  });

  it("rejects compressed payloads with missing or mismatched lengths", () => {
    const payload = compressedPayload();
    expect(() =>
      parseAsciiFrame(payload.subarray(0, payload.length - 1)),
    ).toThrow("Frame payload size mismatch");
    new DataView(payload.buffer).setUint32(12, 0, false);
    expect(() => parseAsciiFrame(payload)).toThrow(
      "Frame payload size mismatch",
    );
  });

  it("rejects a decompressed size that differs from the header", () => {
    const payload = compressedPayload();
    new DataView(payload.buffer).setUint32(8, 5, false);
    expect(() => parseAsciiFrame(payload)).toThrow(
      "Decompressed ASCII frame size mismatch",
    );
  });

  it("rejects corrupt compressed data", () => {
    const payload = compressedPayload();
    payload[ASCII_FRAME_HEADER_SIZE] = 0;
    expect(() => parseAsciiFrame(payload)).toThrow();
  });

  it("rejects empty frame data before allocation", () => {
    expect(() => parseAsciiFrame(buildFramePayload(0, 0, ""))).toThrow("decoded byte budget");
  });

  it("should handle large dimensions", () => {
    const frameData = "X".repeat(200);
    const payload = buildFramePayload(200, 60, frameData);

    const result = parseAsciiFrame(payload);
    expect(result.header.width).toBe(200);
    expect(result.header.height).toBe(60);
    expect(result.ansiString.length).toBe(200);
  });

  it("should correctly read from a Uint8Array with byte offset", () => {
    // Simulate a slice from a larger buffer (like after packet header extraction)
    const frameData = "Offset test";
    const inner = buildFramePayload(80, 24, frameData);

    // Wrap in a larger buffer with prefix bytes
    const outer = new Uint8Array(16 + inner.length);
    outer.set(inner, 16);

    // Create a subarray (shares underlying ArrayBuffer, has byteOffset)
    const sliced = outer.subarray(16);

    const result = parseAsciiFrame(sliced);
    expect(result.ansiString).toBe(frameData);
  });
});
