import { beforeEach, afterEach, expect, it, vi } from "vite-plus/test";
import {
  LocalRecording,
  RECORDING_LIMIT_BYTES,
  RECORDING_LIMIT_MS,
  recordingMimeType,
} from "../../src/recording/LocalRecording";

class Track extends EventTarget {
  stop = vi.fn();
  clone = vi.fn(() => new Track());
}
class Stream {
  tracks: Track[];
  constructor(tracks: Track[] = []) {
    this.tracks = tracks;
  }
  addTrack(track: Track) {
    this.tracks.push(track);
  }
  getTracks() {
    return this.tracks;
  }
  getAudioTracks() {
    return this.tracks.filter((track) => track !== video);
  }
  getVideoTracks() {
    return this.tracks.filter((track) => track === video);
  }
}
let video: Track;
let microphone: Track;
let encoder: Encoder;
let drawImage: ReturnType<typeof vi.fn>;
class Encoder {
  static isTypeSupported = vi.fn((_type: string) => true);
  state = "inactive";
  mimeType = "video/webm;codecs=vp9,opus";
  ondataavailable: ((event: { data: Blob }) => void) | null = null;
  onstop: (() => void) | null = null;
  onerror: (() => void) | null = null;
  constructor() {
    encoder = this;
  }
  start = vi.fn(() => {
    this.state = "recording";
  });
  stop = vi.fn(() => {
    this.state = "inactive";
  });
  finalChunk() {
    this.ondataavailable?.({ data: new Blob(["encoded-final-frame"]) });
    this.onstop?.();
  }
}
beforeEach(() => {
  vi.useFakeTimers();
  video = new Track();
  microphone = new Track();
  drawImage = vi.fn();
  Encoder.isTypeSupported.mockReturnValue(true);
  vi.stubGlobal("MediaRecorder", Encoder);
  vi.stubGlobal(
    "requestAnimationFrame",
    vi.fn(() => 1),
  );
  vi.stubGlobal("cancelAnimationFrame", vi.fn());
  vi.spyOn(document, "createElement").mockReturnValue({
    width: 0,
    height: 0,
    getContext: () => ({ fillRect: vi.fn(), drawImage }),
    captureStream: () => new Stream([video]),
  } as unknown as HTMLCanvasElement);
});
afterEach(() => {
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  vi.useRealTimers();
});
const canvas = () =>
  ({
    width: 640,
    height: 480,
    captureStream: vi.fn(),
  }) as unknown as HTMLCanvasElement;
const audio = () => new Stream([microphone]) as unknown as MediaStream;

it("copies only displayed pixels and waits for the final encoded chunk", async () => {
  const state = vi.fn();
  const owner = new LocalRecording(state, vi.fn());
  const displayed = canvas();
  owner.start(displayed, audio());
  expect(drawImage.mock.calls[0]?.[0]).toBe(displayed);
  expect(displayed.captureStream).not.toHaveBeenCalled();
  const result = owner.stop();
  void owner.stop();
  expect(encoder.stop).toHaveBeenCalledTimes(1);
  expect(state).toHaveBeenLastCalledWith("finalizing");
  encoder.finalChunk();
  expect((await result)?.blob.size).toBeGreaterThan(0);
  expect(state).toHaveBeenLastCalledWith("idle");
  expect(video.stop).toHaveBeenCalledOnce();
  expect(microphone.stop).not.toHaveBeenCalled();
  expect(microphone.clone.mock.results[0]?.value.stop).toHaveBeenCalledOnce();
});

it("rejects unsupported codecs before capturing media", () => {
  Encoder.isTypeSupported.mockReturnValue(false);
  expect(recordingMimeType()).toBeNull();
  const owner = new LocalRecording(vi.fn(), vi.fn());
  expect(() => owner.start(canvas(), audio())).toThrow("cannot record");
  expect(microphone.clone).not.toHaveBeenCalled();
});

it("falls back to browser-supported MP4 and cleans up failed encoder startup", () => {
  Encoder.isTypeSupported.mockImplementation(
    (type: string) => type === "video/mp4",
  );
  expect(recordingMimeType()).toBe("video/mp4");
  vi.stubGlobal(
    "MediaRecorder",
    class extends Encoder {
      constructor() {
        super();
        throw new Error("encoder unavailable");
      }
    },
  );
  const owner = new LocalRecording(vi.fn(), vi.fn());
  expect(() => owner.start(canvas(), audio())).toThrow("encoder unavailable");
  expect(video.stop).toHaveBeenCalledOnce();
  expect(microphone.stop).not.toHaveBeenCalled();
  expect(microphone.clone.mock.results[0]?.value.stop).toHaveBeenCalledOnce();
});

it("finalizes a near-limit clip while reserving space for the last chunk", async () => {
  const owner = new LocalRecording(vi.fn(), vi.fn());
  owner.start(canvas(), audio());
  const chunk = new Blob(["encoded clip"]);
  Object.defineProperty(chunk, "size", {
    value: RECORDING_LIMIT_BYTES - 8 * 1024 * 1024,
  });
  encoder.ondataavailable?.({ data: chunk });
  expect(encoder.stop).toHaveBeenCalledOnce();
  encoder.finalChunk();
  expect(await owner.completed()).not.toBeNull();
});

it("rejects browser encoding failures rather than offering a partial download", async () => {
  const error = vi.fn();
  const owner = new LocalRecording(vi.fn(), error);
  owner.start(canvas(), audio());
  encoder.onerror?.();
  encoder.finalChunk();
  expect(await owner.completed()).toBeNull();
  expect(error).toHaveBeenCalledWith(expect.stringContaining("encoder failed"));
});

it("stops at the duration bound and finalizes on an ended input", async () => {
  const owner = new LocalRecording(vi.fn(), vi.fn());
  owner.start(canvas(), audio());
  microphone.dispatchEvent(new Event("ended"));
  expect(encoder.stop).toHaveBeenCalledOnce();
  encoder.finalChunk();
  expect(await owner.completed()).not.toBeNull();
  const timed = new LocalRecording(vi.fn(), vi.fn());
  timed.start(canvas(), audio());
  vi.advanceTimersByTime(RECORDING_LIMIT_MS);
  expect(encoder.stop).toHaveBeenCalledOnce();
  encoder.finalChunk();
  expect(await timed.completed()).not.toBeNull();
});

it("drops oversized output and releases all capture tracks", async () => {
  const error = vi.fn();
  const owner = new LocalRecording(vi.fn(), error);
  owner.start(canvas(), audio());
  encoder.ondataavailable?.({
    data: { size: RECORDING_LIMIT_BYTES + 1 } as Blob,
  });
  expect(encoder.stop).toHaveBeenCalledOnce();
  encoder.finalChunk();
  expect(await owner.completed()).toBeNull();
  expect(error).toHaveBeenCalledWith(expect.stringContaining("memory limit"));
  expect(video.stop).toHaveBeenCalledOnce();
});

it("bounds an encoder that never sends stop, and suppresses late events after disposal", async () => {
  const error = vi.fn();
  const owner = new LocalRecording(vi.fn(), error);
  owner.start(canvas(), audio());
  const completed = owner.stop();
  vi.advanceTimersByTime(10_000);
  expect(await completed).toBeNull();
  expect(error).toHaveBeenCalledWith(expect.stringContaining("did not finish"));
  const state = vi.fn();
  const disposed = new LocalRecording(state, error);
  disposed.start(canvas(), audio());
  disposed.dispose();
  state.mockClear();
  error.mockClear();
  encoder.finalChunk();
  expect(await disposed.completed()).toBeNull();
  expect(state).not.toHaveBeenCalled();
  expect(error).not.toHaveBeenCalled();
});
