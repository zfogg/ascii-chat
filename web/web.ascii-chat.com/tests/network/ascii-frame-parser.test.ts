/**
 * Unit tests for AsciiFrameParser
 */

import { describe, it, expect } from "vitest";
import {
  parseAsciiFrame,
  ASCII_FRAME_HEADER_SIZE,
  FrameFlags,
} from "../../src/network/AsciiFrameParser";

function buildFramePayload(
  width: number,
  height: number,
  frameData: string,
  flags = FrameFlags.HAS_COLOR,
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

  it("should throw on compressed frames", () => {
    const frameData = "test";
    const payload = buildFramePayload(
      4,
      1,
      frameData,
      FrameFlags.IS_COMPRESSED,
    );

    expect(() => parseAsciiFrame(payload)).toThrow(
      "Compressed ASCII frames not supported",
    );
  });

  it("should handle empty frame data", () => {
    const payload = buildFramePayload(0, 0, "");

    const result = parseAsciiFrame(payload);
    expect(result.header.width).toBe(0);
    expect(result.header.height).toBe(0);
    expect(result.header.originalSize).toBe(0);
    expect(result.ansiString).toBe("");
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
