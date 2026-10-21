import {
  afterEach,
  beforeEach,
  describe,
  expect,
  it,
  vi,
} from "vite-plus/test";
import { VideoUploadEncoder as H265Encoder } from "../../src/network/VideoUploadEncoder";
import {
  getVideoEncoding,
  selectVideoEncoder,
} from "../../src/network/videoEncoding";
import { buildCapabilitiesPacket } from "../../src/network/packetBuilders";

class FakeEncoder {
  static instances: FakeEncoder[] = [];
  static isConfigSupported = vi.fn(async (config: VideoEncoderConfig) => ({
    supported: true,
    config,
  }));
  state = "unconfigured";
  encodeQueueSize = 0;
  configure = vi.fn((_config: VideoEncoderConfig) => {
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
  vi.stubGlobal(
    "VideoFrame",
    class {
      close() {}
    },
  );
  vi.stubGlobal(
    "OffscreenCanvas",
    class {
      constructor(
        public width: number,
        public height: number,
      ) {}
      getContext() {
        return { drawImage: vi.fn() };
      }
    },
  );
});
afterEach(() => vi.unstubAllGlobals());

describe("HEVC uploads", () => {
  it.each(["hevc", "h264"] as const)(
    "keeps %s output alive while an adaptive probe is pending or unsupported",
    async (codec) => {
      let now = 0;
      const clock = vi.spyOn(performance, "now").mockImplementation(() => now);
      const output = vi.fn();
      const encoder = new H265Encoder(output, codec);
      try {
        await encoder.initialize(320, 240, 30);
        const active = FakeEncoder.instances[0]!;
        active.emit();
        let resolveProbe!: (result: { supported: boolean }) => void;
        FakeEncoder.isConfigSupported.mockImplementation(
          (config) =>
            new Promise((resolve) => {
              resolveProbe = (result) => resolve({ ...result, config });
            }),
        );
        now = 500;
        encoder.observeTransport({ bufferedBytes: 300000 });
        encoder.encode(frame());
        expect(active.close).not.toHaveBeenCalled();
        now = 550;
        encoder.encode(frame());
        active.emit([0, 0, 1, 2], "delta");
        expect(output.mock.calls.at(-1)![0]).toMatchObject({
          width: 320,
          height: 240,
          flags: 0,
        });
        expect(active.encode).toHaveBeenCalledTimes(2);
        resolveProbe({ supported: false });
        await new Promise((resolve) => setTimeout(resolve, 0));
        now = 600;
        expect(() => encoder.encode(frame())).not.toThrow();
        now = 650;
        encoder.encode(frame());
        expect(active.close).not.toHaveBeenCalled();
        expect(active.encode).toHaveBeenCalledTimes(4);
        expect(FakeEncoder.instances).toHaveLength(1);
        // Rejected tiers do not restart a probe on every camera callback.
        expect(FakeEncoder.isConfigSupported).toHaveBeenCalledTimes(2);
        expect(encoder.qualityStatus).toMatchObject({
          width: 320,
          height: 240,
          framerate: 30,
        });
        now = 1000;
        encoder.observeTransport({ bufferedBytes: 300000 });
        encoder.encode(frame());
        encoder.destroy();
        resolveProbe({ supported: true });
        await new Promise((resolve) => setTimeout(resolve, 0));
        expect(FakeEncoder.instances).toHaveLength(1);
        expect(active.close).toHaveBeenCalledOnce();
      } finally {
        encoder.destroy();
        clock.mockRestore();
      }
    },
  );
  it("retains the active encoder if a supported adaptive candidate fails to configure", async () => {
    let now = 0;
    const clock = vi.spyOn(performance, "now").mockImplementation(() => now);
    class RejectSmallEncoder extends FakeEncoder {
      override configure = vi.fn((config: VideoEncoderConfig) => {
        if (config.width === 240 && FakeEncoder.instances.length === 2)
          throw new Error("device rejected adaptive size");
        this.state = "configured";
      });
    }
    vi.stubGlobal("VideoEncoder", RejectSmallEncoder);
    const output = vi.fn();
    const encoder = new H265Encoder(output);
    try {
      await encoder.initialize(320, 240, 30);
      const active = FakeEncoder.instances[0]!;
      now = 500;
      encoder.observeTransport({ bufferedBytes: 300000 });
      encoder.encode(frame());
      await vi.waitFor(() => expect(FakeEncoder.instances).toHaveLength(2));
      expect(FakeEncoder.instances[1]!.close).toHaveBeenCalledOnce();
      expect(active.close).not.toHaveBeenCalled();
      now = 600;
      expect(() => encoder.encode(frame())).not.toThrow();
      active.emit();
      expect(output.mock.calls.at(-1)![0]).toMatchObject({
        width: 320,
        height: 240,
        flags: 3,
      });
      now = 1000;
      encoder.observeTransport({ bufferedBytes: 300000 });
      encoder.encode(frame());
      await vi.waitFor(() => expect(FakeEncoder.instances).toHaveLength(3));
      const rejected = FakeEncoder.instances[1]!;
      rejected.callbacks.error(
        new DOMException("late rejected candidate error"),
      );
      rejected.emit();
      expect(output).toHaveBeenCalledOnce();
      now = 1100;
      expect(() => encoder.encode(frame())).not.toThrow();
      FakeEncoder.instances[2]!.emit();
      expect(output.mock.calls.at(-1)![0]).toMatchObject({
        width: 240,
        height: 180,
        flags: 3,
      });
    } finally {
      encoder.destroy();
      clock.mockRestore();
    }
  });
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
  it("scales adaptive uploads, keeps the original FPS ceiling, and restarts with a keyframe", async () => {
    let now = 0;
    const clock = vi.spyOn(performance, "now").mockImplementation(() => now);
    const drawImage = vi.fn(),
      close = vi.fn();
    vi.stubGlobal(
      "OffscreenCanvas",
      class {
        constructor(
          public width: number,
          public height: number,
        ) {}
        getContext() {
          return { drawImage };
        }
      },
    );
    vi.stubGlobal(
      "VideoFrame",
      class {
        displayWidth: number;
        displayHeight: number;
        close = close;
        constructor(canvas: OffscreenCanvas) {
          this.displayWidth = canvas.width;
          this.displayHeight = canvas.height;
        }
      },
    );
    try {
      const output = vi.fn();
      const encoder = new H265Encoder(output);
      await encoder.initialize(320, 240, 30);
      const original = FakeEncoder.instances[0]!;
      now = 500;
      encoder.observeTransport({ bufferedBytes: 300000 });
      encoder.encode(frame());
      await vi.waitFor(() => expect(FakeEncoder.instances).toHaveLength(2));
      original.emit();
      expect(output).not.toHaveBeenCalled();
      const adapted = FakeEncoder.instances[1]!;
      now = 600;
      encoder.encode(frame());
      expect(adapted.encode).toHaveBeenCalledWith(
        expect.objectContaining({ displayWidth: 240, displayHeight: 180 }),
        { keyFrame: true },
      );
      expect(drawImage).toHaveBeenCalledWith(frame(), 0, 0, 240, 180);
      expect(close).toHaveBeenCalledOnce();
      now = 610;
      encoder.encode(frame(), 30, true);
      expect(adapted.encode).toHaveBeenCalledOnce();
      now = 650;
      encoder.encode(frame(), 30, true);
      expect(adapted.encode).toHaveBeenCalledTimes(2);
      expect(adapted.encode.mock.calls.at(-1)![1]).toEqual({ keyFrame: true });
      adapted.emit();
      expect(output.mock.calls[0]![0]).toMatchObject({
        width: 240,
        height: 180,
        flags: 3,
      });
      encoder.destroy();
    } finally {
      clock.mockRestore();
    }
  });
  it("coalesces small bitrate-only recovery steps but always restores the ceiling", async () => {
    let now = 0;
    const clock = vi.spyOn(performance, "now").mockImplementation(() => now);
    vi.stubGlobal(
      "OffscreenCanvas",
      class {
        constructor(
          public width: number,
          public height: number,
        ) {}
        getContext() {
          return { drawImage: vi.fn() };
        }
      },
    );
    vi.stubGlobal(
      "VideoFrame",
      class {
        close() {}
      },
    );
    try {
      const encoder = new H265Encoder(vi.fn());
      await encoder.initialize(320, 240, 1);
      now = 500;
      encoder.observeTransport({ bufferedBytes: 300000 });
      encoder.encode(frame());
      await vi.waitFor(() => expect(FakeEncoder.instances).toHaveLength(2));
      for (now = 1000; now <= 6000; now += 500)
        encoder.observeTransport({ bufferedBytes: 0 });
      encoder.encode(frame());
      expect(FakeEncoder.instances).toHaveLength(2);
      expect(encoder.qualityStatus.bitrate).toBe(41600);
      for (let recovery = 11000; recovery <= 36000; recovery += 5000) {
        now = recovery;
        encoder.observeTransport({ bufferedBytes: 0 });
        encoder.encode(frame());
        await new Promise((resolve) => setTimeout(resolve, 0));
      }
      expect(encoder.qualityStatus.bitrate).toBe(64000);
      encoder.destroy();
    } finally {
      clock.mockRestore();
    }
  });
  it("preserves requested cadence across slightly early timer callbacks", async () => {
    let now = 0;
    const clock = vi.spyOn(performance, "now").mockImplementation(() => now);
    try {
      const encoder = new H265Encoder(vi.fn());
      await encoder.initialize(320, 240, 30);
      for (let i = 0; i < 30; i++) {
        now = (i * 1000) / 30 - (i % 2 ? 0.2 : 0);
        encoder.encode(frame());
      }
      expect(FakeEncoder.instances[0]!.encode).toHaveBeenCalledTimes(30);
      encoder.destroy();
    } finally {
      clock.mockRestore();
    }
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

describe("AVC and automatic selection", () => {
  it("accepts H.264 case-insensitively and its common aliases", () => {
    for (const value of ["H.264", "h264", "avc", "x264"])
      expect(getVideoEncoding(`?encoding=${value}`)).toBe("h264");
  });
  it("uses AVC Annex B with a startup keyframe", async () => {
    const encoder = await selectVideoEncoder("h264", 320, 240, 30, vi.fn());
    expect(encoder?.codec).toBe("h264");
    expect(FakeEncoder.isConfigSupported).toHaveBeenLastCalledWith(
      expect.objectContaining({
        codec: "avc1.42E01F",
        avc: { format: "annexb" },
      }),
    );
    encoder!.encode(frame());
    expect(FakeEncoder.instances[0]!.encode).toHaveBeenCalledWith(frame(), {
      keyFrame: true,
    });
    encoder!.destroy();
  });
  it("prefers HEVC then AVC then raw, probing each configuration", async () => {
    const hevc = await selectVideoEncoder("auto", 320, 240, 30, vi.fn());
    expect(hevc?.codec).toBe("hevc");
    hevc?.destroy();
    FakeEncoder.isConfigSupported.mockImplementation(async (config) => ({
      supported: config.codec.startsWith("avc1"),
      config,
    }));
    const avc = await selectVideoEncoder("auto", 320, 240, 30, vi.fn());
    expect(avc?.codec).toBe("h264");
    avc?.destroy();
    FakeEncoder.isConfigSupported.mockImplementation(async (config) => ({
      supported: false,
      config,
    }));
    expect(await selectVideoEncoder("auto", 320, 240, 30, vi.fn())).toBeNull();
    await expect(
      selectVideoEncoder("h264", 320, 240, 30, vi.fn()),
    ).rejects.toThrow(/H.264.*unsupported/);
  });
  it("does not retry a failed codec during automatic runtime fallback", async () => {
    const avc = await selectVideoEncoder("auto", 320, 240, 30, vi.fn(), "hevc");
    expect(avc?.codec).toBe("h264");
    avc?.destroy();
    expect(
      await selectVideoEncoder("auto", 320, 240, 30, vi.fn(), "h264"),
    ).toBeNull();
  });
  it("rejects an ignored AVC Annex B option", async () => {
    FakeEncoder.isConfigSupported.mockImplementation(async (config) => ({
      supported: true,
      config: { codec: config.codec, width: 320, height: 240 },
    }));
    await expect(
      selectVideoEncoder("h264", 320, 240, 30, vi.fn()),
    ).rejects.toThrow(/H.264.*unsupported/);
  });
});
