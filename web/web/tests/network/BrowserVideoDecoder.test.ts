import { afterEach, describe, expect, it, vi } from "vitest";
import { BrowserVideoDecoder } from "../../src/network/BrowserVideoDecoder";
import { PacketType } from "../../src/wasm/client";

function packet(flags = 1) {
  return new Uint8Array([flags, 0, 2, 0, 2, 0, 0, 0, 1, 5]);
}
afterEach(() => vi.unstubAllGlobals());
describe("browser host video decoding", () => {
  it.each([
    [PacketType.IMAGE_FRAME_H264, "avc1.42E01F"],
    [PacketType.IMAGE_FRAME_H265, "hev1.1.6.L120.B0"],
  ])("decodes codec %s and supplies pixels to C host", async (type, codec) => {
    const configured = vi.fn();
    const decoded = vi.fn();
    let output!: (frame: VideoFrame) => void;
    vi.stubGlobal(
      "VideoDecoder",
      class {
        state = "configured";
        constructor(init: VideoDecoderInit) {
          output = init.output;
        }
        configure = configured;
        decode = decoded;
        close() {
          this.state = "closed";
        }
      },
    );
    vi.stubGlobal(
      "EncodedVideoChunk",
      class {
        constructor(public init: EncodedVideoChunkInit) {}
      },
    );
    const pixels = vi.fn();
    const failure = vi.fn();
    const decoder = new BrowserVideoDecoder(pixels, failure);
    decoder.receive(type as number, packet());
    expect(configured).toHaveBeenCalledWith(
      expect.objectContaining({ codec, codedWidth: 2, codedHeight: 2 }),
    );
    expect(decoded).toHaveBeenCalledWith(
      expect.objectContaining({
        init: expect.objectContaining({ type: "key", data: packet().slice(5) }),
      }),
    );
    const close = vi.fn();
    output({
      displayWidth: 2,
      displayHeight: 2,
      copyTo: async (target: Uint8Array) => {
        target.fill(127);
      },
      close,
    } as unknown as VideoFrame);
    await vi.waitFor(() => expect(pixels).toHaveBeenCalledOnce());
    expect(pixels.mock.calls[0]![0].slice(24)).toEqual(
      new Uint8Array(12).fill(127),
    );
    expect(failure).not.toHaveBeenCalled();
    decoder.destroy();
  });
  it("rejects truncated uploads and unavailable WebCodecs", () => {
    const decoder = new BrowserVideoDecoder(vi.fn(), vi.fn());
    expect(() =>
      decoder.receive(PacketType.IMAGE_FRAME_H264, new Uint8Array(3)),
    ).toThrow("Truncated");
    vi.stubGlobal("VideoDecoder", undefined);
    expect(() =>
      decoder.receive(PacketType.IMAGE_FRAME_H265, packet()),
    ).toThrow("requires WebCodecs");
  });
});
