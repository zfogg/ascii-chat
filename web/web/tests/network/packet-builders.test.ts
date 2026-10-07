import { describe, expect, it } from "vitest";
import {
  buildCapabilitiesPacket,
  buildImageFramePayload,
} from "../../src/network/packetBuilders";

describe("buildImageFramePayload", () => {
  it("writes the native 24-byte RGB24 image header before pixel data", () => {
    const rgba = new Uint8Array([10, 20, 30, 255, 40, 50, 60, 255]);

    const payload = buildImageFramePayload(rgba, 2, 1);
    const view = new DataView(
      payload.buffer,
      payload.byteOffset,
      payload.byteLength,
    );

    expect(payload.byteLength).toBe(24 + 2 * 3);
    expect(view.getUint32(0, false)).toBe(2);
    expect(view.getUint32(4, false)).toBe(1);
    expect(view.getUint32(8, false)).toBe(1); // RGB24
    expect(view.getUint32(12, false)).toBe(0); // uncompressed
    expect(view.getUint32(16, false)).toBe(0); // checksum
    expect(view.getUint32(20, false)).toBe(0); // timestamp
    expect(Array.from(payload.subarray(24))).toEqual([10, 20, 30, 40, 50, 60]);
  });
});

describe("buildCapabilitiesPacket", () => {
  it("advertises browser truecolor by default", () => {
    const packet = buildCapabilitiesPacket(80, 24);
    const view = new DataView(
      packet.buffer,
      packet.byteOffset,
      packet.byteLength,
    );

    expect(view.getUint32(4, false)).toBe(3);
    expect(view.getUint32(8, false)).toBe(16_777_216);
  });

  it("preserves an explicit color-mode choice", () => {
    const packet = buildCapabilitiesPacket(80, 24, 60, "256");
    const view = new DataView(
      packet.buffer,
      packet.byteOffset,
      packet.byteLength,
    );

    expect(view.getUint32(4, false)).toBe(2);
    expect(view.getUint32(8, false)).toBe(256);
  });

  it("serializes live palette and filter settings for the server", () => {
    const packet = buildCapabilitiesPacket(
      80,
      24,
      45,
      "truecolor",
      "rainbow",
      "custom",
      " .#",
    );
    const view = new DataView(
      packet.buffer,
      packet.byteOffset,
      packet.byteLength,
    );

    expect(view.getUint32(89, false)).toBe(6); // PALETTE_CUSTOM
    expect(new TextDecoder().decode(packet.subarray(93, 96))).toBe(" .#");
    expect(packet[157]).toBe(45);
    expect(packet[158]).toBe(12); // COLOR_FILTER_RAINBOW
  });
});
