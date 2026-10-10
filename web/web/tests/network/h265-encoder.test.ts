import {
  afterEach,
  beforeEach,
  describe,
  expect,
  it,
  vi,
} from "vite-plus/test";
import { H265Encoder } from "../../src/network/H265Encoder";
import { getVideoEncoding } from "../../src/network/videoEncoding";
import { buildCapabilitiesPacket } from "../../src/network/packetBuilders";

class FakeEncoder {
  static instances: FakeEncoder[] = [];
  static isConfigSupported = vi.fn(async (config: VideoEncoderConfig) => ({
    supported: true,
    config,
  }));
  state = "unconfigured";
  encodeQueueSize = 0;
  configure = vi.fn(() => {
    this.state = "configured";
  });
  encode = vi.fn();
  close = vi.fn(() => {
    this.state = "closed";
  });
  constructor(readonly callbacks: VideoEncoderInit) {
    FakeEncoder.instances.push(this);
  }
  emit(bytes = [0, 0, 0, 1, 64], type: EncodedVideoChunkType = "key") {
    this.callbacks.output(
      {
        type,
        byteLength: bytes.length,
        copyTo: (target: Uint8Array) => target.set(bytes),
      } as unknown as EncodedVideoChunk,
      {},
    );
  }
}
const frame = (width = 320, height = 240) =>
  ({ displayWidth: width, displayHeight: height }) as VideoFrame;

beforeEach(() => {
  FakeEncoder.instances = [];
  FakeEncoder.isConfigSupported.mockImplementation(async (config) => ({
    supported: true,
    config,
  }));
  vi.stubGlobal("VideoEncoder", FakeEncoder);
  vi.stubGlobal("VideoFrame", class {});
});
afterEach(() => vi.unstubAllGlobals());

describe("HEVC uploads", () => {
  it("probes the exact Annex B configuration and starts with a keyframe", async () => {
    const output = vi.fn();
    const encoder = new H265Encoder(output);
    await encoder.initialize(320, 240, 30);
    const native = FakeEncoder.instances[0]!;
    expect(native.configure.mock.calls[0]).toEqual(
      FakeEncoder.isConfigSupported.mock.calls.at(-1),
    );
    encoder.encode(frame());
    expect(native.encode).toHaveBeenCalledWith(frame(), { keyFrame: true });
    native.emit();
    native.emit([0, 0, 1, 2], "delta");
    expect(output.mock.calls.map(([chunk]) => chunk.flags)).toEqual([3, 0]);
    expect(output.mock.calls[0]![0]).toMatchObject({ width: 320, height: 240 });
    encoder.destroy();
  });
  it("rejects unsupported configurations and ignored Annex B options", async () => {
    const encoder = new H265Encoder(vi.fn());
    FakeEncoder.isConfigSupported.mockImplementation(async (config) => ({
      supported: false,
      config,
    }));
    await expect(encoder.initialize(320, 240, 30)).rejects.toThrow(
      /unsupported.*encoding=raw/,
    );
    FakeEncoder.isConfigSupported.mockImplementation(async (config) => ({
      supported: true,
      config: { codec: config.codec, width: 320, height: 240 },
    }));
    await expect(encoder.initialize(320, 240, 30)).rejects.toThrow(
      /unsupported/,
    );
    expect(FakeEncoder.instances).toHaveLength(0);
  });
  it("reports async failures and rejects length-prefixed output", async () => {
    const output = vi.fn();
    const encoder = new H265Encoder(output);
    await encoder.initialize(320, 240, 30);
    FakeEncoder.instances[0]!.emit([0, 0, 0, 5, 64]);
    expect(output).not.toHaveBeenCalled();
    expect(() => encoder.encode(frame())).toThrow(/Annex B/);
    await encoder.initialize(320, 240, 30);
    FakeEncoder.instances[1]!.callbacks.error(
      new DOMException("encoder failed"),
    );
    expect(() => encoder.encode(frame())).toThrow("encoder failed");
    encoder.destroy();
  });
  it("closes the encoder and reports configuration failures", async () => {
    class FailingEncoder extends FakeEncoder {
      override configure = vi.fn(() => {
        throw new Error("hardware unavailable");
      });
    }
    vi.stubGlobal("VideoEncoder", FailingEncoder);
    const encoder = new H265Encoder(vi.fn());
    await expect(encoder.initialize(320, 240, 30)).rejects.toThrow(
      /HEVC.*hardware unavailable.*encoding=raw/,
    );
    expect(FakeEncoder.instances[0]!.close).toHaveBeenCalledOnce();
  });
  it("drops inputs under load without dropping encoded output or keyframe requests", async () => {
    const output = vi.fn();
    const encoder = new H265Encoder(output);
    await encoder.initialize(320, 240, 30);
    const native = FakeEncoder.instances[0]!;
    native.encodeQueueSize = 2;
    encoder.encode(frame());
    expect(native.encode).not.toHaveBeenCalled();
    native.encodeQueueSize = 0;
    encoder.encode(frame());
    expect(native.encode).toHaveBeenCalledWith(frame(), { keyFrame: true });
    native.emit();
    native.emit([0, 0, 1, 2], "delta");
    expect(output).toHaveBeenCalledTimes(2);
    encoder.destroy();
  });
  it("recreates on resize and suppresses stale output after resize and stop", async () => {
    const output = vi.fn();
    const encoder = new H265Encoder(output);
    await encoder.initialize(320, 240, 30);
    const old = FakeEncoder.instances[0]!;
    encoder.encode(frame(640, 360));
    await vi.waitFor(() => expect(FakeEncoder.instances).toHaveLength(2));
    old.emit();
    expect(output).not.toHaveBeenCalled();
    encoder.encode(frame(640, 360));
    const resized = FakeEncoder.instances[1]!;
    expect(resized.encode).toHaveBeenCalledWith(frame(640, 360), {
      keyFrame: true,
    });
    resized.emit();
    expect(output.mock.calls[0]![0]).toMatchObject({
      width: 640,
      height: 360,
      flags: 3,
    });
    encoder.destroy();
    resized.emit();
    expect(output).toHaveBeenCalledTimes(1);
  });
  it("does not resurrect an encoder stopped during probing", async () => {
    let resolve!: (value: {
      supported: boolean;
      config: VideoEncoderConfig;
    }) => void;
    FakeEncoder.isConfigSupported.mockImplementation(
      (config) =>
        new Promise((done) => {
          resolve = (value) => done({ ...value, config });
        }),
    );
    const encoder = new H265Encoder(vi.fn());
    const pending = encoder.initialize(320, 240, 30);
    encoder.destroy();
    resolve({
      supported: true,
      config: { codec: "hev1", width: 320, height: 240 },
    });
    await pending;
    expect(FakeEncoder.instances).toHaveLength(0);
  });
});

it("parses explicit codecs and rejects invalid values", () => {
  expect(getVideoEncoding("")).toBe("auto");
  for (const value of ["hevc", "hvec", "h265"])
    expect(getVideoEncoding(`?encoding=${value}`)).toBe("hevc");
  expect(getVideoEncoding("?encoding=raw")).toBe("raw");
  expect(() => getVideoEncoding("?encoding=bogus")).toThrow(/Unknown encoding/);
});
it("advertises HEVC only after a successful per-session probe", () => {
  const defaults = buildCapabilitiesPacket(80, 40);
  expect(new DataView(defaults.buffer).getUint32(160)).toBe(1);
  const hevc = buildCapabilitiesPacket(
    80,
    40,
    30,
    "truecolor",
    "none",
    "standard",
    undefined,
    false,
    3,
  );
  expect(new DataView(hevc.buffer).getUint32(160)).toBe(3);
});
