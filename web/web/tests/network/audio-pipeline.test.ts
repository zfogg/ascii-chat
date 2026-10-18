import {
  afterEach,
  beforeEach,
  describe,
  expect,
  it,
  vi,
} from "vite-plus/test";
import { AudioPipeline } from "../../src/audio/AudioPipeline";
import { PacketType } from "../../src/wasm/client";

vi.mock("../../src/wasm/client", () => ({
  getClientModule: () => ({}),
  PacketType: { AUDIO_BATCH: 4, AUDIO_OPUS_BATCH: 5 },
}));
// Host resource tests isolate the C boundary; native and browser integration tests
// exercise the actual shared state machine.
vi.mock("../../src/wasm/MediaLifecycle", () => ({
  MediaLifecycle: class {
    generation = 0;
    active = false;
    pending = false;
    begin() {
      if (this.active || this.pending) return 0;
      this.pending = true;
      return ++this.generation;
    }
    current(token: number) {
      return token === this.generation && (this.pending || this.active);
    }
    complete(token: number) {
      if (!this.current(token)) return false;
      this.pending = false;
      return (this.active = true);
    }
    stop(release: () => void) {
      this.active = this.pending = false;
      release();
    }
    dispose() {}
  },
}));
vi.mock("../../src/audio/OpusEncoder", () => ({
  OpusEncoder: class {
    init = vi.fn().mockResolvedValue(undefined);
    encode = vi.fn(() => new Uint8Array([1, 2, 3]));
    decode = vi.fn(() => new Int16Array(960).fill(8192));
    cleanup = vi.fn();
  },
}));

const node = () => ({ connect: vi.fn(), disconnect: vi.fn() });
let processor: ReturnType<typeof node> & {
  onaudioprocess: ((event: AudioProcessingEvent) => void) | null;
};
let start: ReturnType<typeof vi.fn>;
let stopTrack: ReturnType<typeof vi.fn>;
let getUserMedia: ReturnType<typeof vi.fn>;

beforeEach(() => {
  processor = { ...node(), onaudioprocess: null };
  start = vi.fn();
  stopTrack = vi.fn();
  getUserMedia = vi.fn().mockResolvedValue({
    getTracks: () => [{ stop: stopTrack }],
  });
  vi.stubGlobal("navigator", { mediaDevices: { getUserMedia } });
  vi.stubGlobal(
    "AudioContext",
    class {
      currentTime = 0;
      state = "running";
      destination = {};
      resume = vi.fn().mockResolvedValue(undefined);
      close = vi.fn().mockResolvedValue(undefined);
      createMediaStreamSource = node;
      createScriptProcessor = () => processor;
      createGain = () => ({ ...node(), gain: { value: 1 } });
      createBuffer = (_channels: number, length: number, rate: number) => ({
        duration: length / rate,
        getChannelData: () => new Float32Array(length),
      });
      createBufferSource = () => ({ ...node(), start, stop: vi.fn() });
    },
  );
});
afterEach(() => vi.unstubAllGlobals());

describe("AudioPipeline", () => {
  it("captures echo-cancelled microphone audio into native Opus packets", async () => {
    const send = vi.fn();
    const pipeline = new AudioPipeline({ onAudioData: send });
    await pipeline.startCapture();
    expect(getUserMedia).toHaveBeenCalledWith({
      audio: {
        channelCount: 1,
        sampleRate: 48000,
        echoCancellation: true,
        noiseSuppression: true,
        autoGainControl: true,
      },
      video: false,
    });
    processor.onaudioprocess!({
      inputBuffer: { getChannelData: () => new Float32Array(2048).fill(0.25) },
    } as unknown as AudioProcessingEvent);
    expect(send).toHaveBeenCalledTimes(2);
    const payload = send.mock.calls[0]![0] as Uint8Array;
    const header = new DataView(payload.buffer);
    expect(header.getUint32(0)).toBe(48000);
    expect(header.getUint32(4)).toBe(20);
    expect(header.getUint32(8)).toBe(1);
    expect(header.getUint16(16)).toBe(3);
    expect(Array.from(payload.slice(18))).toEqual([1, 2, 3]);
    pipeline.close();
    expect(stopTrack).toHaveBeenCalledOnce();
    expect(processor.onaudioprocess).toBeNull();
  });

  it("plays valid Opus packets and rejects truncated frames", async () => {
    const pipeline = new AudioPipeline();
    await pipeline.enablePlayback();
    const payload = new Uint8Array(21);
    const header = new DataView(payload.buffer);
    header.setUint32(0, 48000);
    header.setUint32(4, 20);
    header.setUint32(8, 1);
    header.setUint16(16, 3);
    pipeline.playPacket(PacketType.AUDIO_OPUS_BATCH, payload);
    expect(start).toHaveBeenCalledWith(0.04);
    pipeline.playPacket(PacketType.AUDIO_OPUS_BATCH, payload);
    expect(start).toHaveBeenLastCalledWith(0.06);
    expect(() =>
      pipeline.playPacket(PacketType.AUDIO_OPUS_BATCH, payload.slice(0, 20)),
    ).toThrow("Truncated Opus frame");
    pipeline.close();
  });

  it("stops a microphone granted after capture was cancelled", async () => {
    let grant!: (stream: unknown) => void;
    getUserMedia.mockImplementation(
      () =>
        new Promise((resolve) => {
          grant = resolve;
        }),
    );
    const pipeline = new AudioPipeline();
    const pending = pipeline.startCapture();
    await vi.waitFor(() => expect(getUserMedia).toHaveBeenCalledOnce());
    pipeline.close();
    grant({ getTracks: () => [{ stop: stopTrack }] });
    await pending;
    expect(stopTrack).toHaveBeenCalledOnce();
    expect(processor.onaudioprocess).toBeNull();
  });
  it("does not create duplicate microphone streams on repeated start", async () => {
    const pipeline = new AudioPipeline();
    await Promise.all([pipeline.startCapture(), pipeline.startCapture()]);
    await pipeline.startCapture();
    expect(getUserMedia).toHaveBeenCalledOnce();
    pipeline.stopCapture();
    await pipeline.startCapture();
    expect(getUserMedia).toHaveBeenCalledTimes(2);
    pipeline.close();
    expect(stopTrack).toHaveBeenCalledTimes(2);
  });

  it("cleans partial capture after node creation fails and permits retry", async () => {
    const pipeline = new AudioPipeline();
    await pipeline.enablePlayback();
    // Our device stub installs methods on the context instance.
    const context = (pipeline as unknown as { context: AudioContext }).context;
    const create = context.createMediaStreamSource;
    context.createMediaStreamSource = () => {
      throw new Error("node startup failed");
    };
    await expect(pipeline.startCapture()).rejects.toThrow(
      "node startup failed",
    );
    expect(stopTrack).toHaveBeenCalledOnce();
    context.createMediaStreamSource = create;
    await pipeline.startCapture();
    pipeline.close();
    expect(stopTrack).toHaveBeenCalledTimes(2);
  });

  it("reports denied capture and allows a subsequent successful start", async () => {
    const pipeline = new AudioPipeline();
    getUserMedia.mockRejectedValueOnce(new Error("permission denied"));
    await expect(pipeline.startCapture()).rejects.toThrow("permission denied");
    await pipeline.startCapture();
    pipeline.close();
    expect(stopTrack).toHaveBeenCalledOnce();
  });
});
