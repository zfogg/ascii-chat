import { afterEach, beforeEach, expect, test, vi } from "vite-plus/test";
import { streamMedia } from "../../src/utils/streamMedia";
import { MAX_MEDIA_FILE_SIZE } from "../../src/utils/mediaUpload";

test("Upload file keeps an 8 GB native video out of the conversion heap", async () => {
  const file = new File(["mp4"], "large.mp4", { type: "video/mp4" });
  Object.defineProperty(file, "size", { value: MAX_MEDIA_FILE_SIZE });
  const result = await streamMedia(
    file,
    document.createElement("video"),
    new AbortController().signal,
    vi.fn(),
    true,
    vi.fn(),
    vi.fn(),
  );
  expect(result).toBe(file);
});

test("Upload file rejects conversion inputs above 8 GB", async () => {
  const file = new File(["avi"], "large.avi");
  Object.defineProperty(file, "size", { value: MAX_MEDIA_FILE_SIZE + 1 });
  await expect(
    streamMedia(
      file,
      document.createElement("video"),
      new AbortController().signal,
      vi.fn(),
      false,
      vi.fn(),
      vi.fn(),
    ),
  ).rejects.toThrow("8 GB");
});

class FakeWorker {
  static current: FakeWorker;
  onmessage?: (event: { data: unknown }) => void;
  onerror?: (event: { message: string }) => void;
  postMessage = vi.fn();
  terminate = vi.fn();
  constructor() {
    FakeWorker.current = this;
  }
  emit(data: unknown) {
    this.onmessage?.({ data });
  }
}
class FakeBuffer extends EventTarget {
  updating = false;
  end = 0;
  get buffered() {
    return { length: this.end ? 1 : 0, start: () => 0, end: () => this.end };
  }
  appendBuffer(data: Uint8Array) {
    this.updating = true;
    queueMicrotask(() => {
      this.end += data[0]!;
      this.updating = false;
      this.dispatchEvent(new Event("updateend"));
    });
  }
}
class FakeSource extends EventTarget {
  static current: FakeSource;
  static isTypeSupported = () => true;
  readyState = "open";
  duration = 40;
  buffer = new FakeBuffer();
  constructor() {
    super();
    FakeSource.current = this;
  }
  addSourceBuffer() {
    return this.buffer;
  }
  endOfStream() {
    this.readyState = "ended";
  }
}
beforeEach(() => {
  vi.stubGlobal("Worker", FakeWorker);
  vi.stubGlobal("MediaSource", FakeSource);
  vi.stubGlobal(
    "URL",
    class extends URL {
      static createObjectURL = vi.fn(() => "blob:stream");
      static revokeObjectURL = vi.fn();
    },
  );
});
afterEach(() => {
  vi.unstubAllGlobals();
  vi.restoreAllMocks();
});
function setup() {
  const video = document.createElement("video");
  let paused = true;
  Object.defineProperties(video, {
    buffered: { get: () => FakeSource.current.buffer.buffered },
    readyState: { get: () => 2 },
    paused: { get: () => paused },
  });
  vi.spyOn(video, "play").mockImplementation(async () => {
    paused = false;
  });
  vi.spyOn(video, "pause").mockImplementation(() => {
    paused = true;
  });
  const controller = new AbortController();
  const status = vi.fn();
  const onPaused = vi.fn();
  const onError = vi.fn();
  const ready = streamMedia(
    new File(["avi"], "movie.avi"),
    video,
    controller.signal,
    status,
    false,
    onPaused,
    onError,
  );
  FakeWorker.current.emit({
    type: "probe",
    probe: { format: { duration: "40" }, streams: [{ codec_type: "video" }] },
  });
  FakeSource.current.dispatchEvent(new Event("sourceopen"));
  FakeWorker.current.emit({ type: "encoding" });
  return { video, controller, status, onPaused, onError, ready };
}
const flush = async () => {
  for (let i = 0; i < 12; i++) await Promise.resolve();
};
test("starts from buffered fragments before worker completion, preserves pause intent, and refills", async () => {
  const { ready, video, onPaused, controller } = setup();
  FakeWorker.current.emit({ type: "data", data: new Uint8Array([4]) });
  await flush();
  const controls = await ready;
  expect(controls).not.toBeNull();
  if (!controls || controls instanceof File)
    throw new Error("Expected streaming controls");
  expect(video.paused).toBe(false);
  expect(FakeSource.current.readyState).toBe("open");
  expect(FakeWorker.current.terminate).not.toHaveBeenCalled();
  video.currentTime = 3.5;
  video.dispatchEvent(new Event("timeupdate"));
  expect(video.paused).toBe(true);
  controls.togglePause();
  expect(onPaused).toHaveBeenLastCalledWith(true);
  FakeWorker.current.emit({ type: "data", data: new Uint8Array([5]) });
  await flush();
  expect(video.paused).toBe(true);
  controls.togglePause();
  expect(video.paused).toBe(false);
  controls.setSuspended(true);
  expect(video.paused).toBe(true);
  FakeWorker.current.emit({ type: "data", data: new Uint8Array([6]) });
  await flush();
  expect(video.paused).toBe(true);
  controls.setSuspended(false);
  expect(video.paused).toBe(false);
  controls.togglePause();
  controls.setSuspended(true);
  controls.setSuspended(false);
  expect(video.paused).toBe(true);
  controller.abort();
  expect(FakeWorker.current.terminate).toHaveBeenCalledOnce();
  expect(URL.revokeObjectURL).toHaveBeenCalled();
});
test("cancel during initial buffering rejects and terminates the worker", async () => {
  const { ready, controller } = setup();
  const rejection = expect(ready).rejects.toMatchObject({ name: "AbortError" });
  controller.abort();
  await rejection;
  expect(FakeWorker.current.terminate).toHaveBeenCalledOnce();
});
test("late conversion failures are surfaced after playback has started", async () => {
  const { ready, onError } = setup();
  FakeWorker.current.emit({ type: "data", data: new Uint8Array([4]) });
  await flush();
  await ready;
  FakeWorker.current.emit({ type: "error", message: "broken frame" });
  expect(onError).toHaveBeenCalledWith(
    expect.objectContaining({ message: "broken frame" }),
  );
  expect(FakeWorker.current.terminate).toHaveBeenCalledOnce();
});

test("short clips wait for EOS and hold the last frame until explicitly restarted", async () => {
  const { ready, video, controller } = setup();
  FakeWorker.current.emit({ type: "data", data: new Uint8Array([1]) });
  await flush();
  expect(video.play).not.toHaveBeenCalled();
  FakeWorker.current.emit({ type: "done" });
  await flush();
  const controls = await ready;
  if (!controls || controls instanceof File)
    throw new Error("Expected controls");
  expect(FakeSource.current.readyState).toBe("ended");
  video.dispatchEvent(new Event("ended"));
  expect(video.loop).toBe(false);
  expect(URL.createObjectURL).toHaveBeenCalledTimes(1);
  video.dispatchEvent(new Event("timeupdate"));
  expect(URL.createObjectURL).toHaveBeenCalledTimes(1);
  controls.restart();
  expect(URL.createObjectURL).toHaveBeenCalledTimes(2);
  expect(video.loop).toBe(false);
  controller.abort();
});
test("browsers without MSE use the existing complete-file converter", async () => {
  vi.stubGlobal("MediaSource", undefined);
  expect(
    await streamMedia(
      new File([], "movie.avi"),
      document.createElement("video"),
      new AbortController().signal,
      vi.fn(),
      false,
      vi.fn(),
      vi.fn(),
    ),
  ).toBeNull();
});
