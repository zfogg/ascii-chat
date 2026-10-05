import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { AudioPipeline } from "../../src/audio/AudioPipeline";
import { PacketType } from "../../src/wasm/client";

vi.mock("../../src/wasm/client", () => ({
  getClientModule: () => ({}),
  PacketType: { AUDIO_BATCH: 4, AUDIO_OPUS_BATCH: 5 },
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
});
