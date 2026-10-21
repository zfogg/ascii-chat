import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, expect, test, vi } from "vite-plus/test";
import { useDisplayCapture } from "../../src/hooks/useDisplayCapture";

function display() {
  const video = Object.assign(new EventTarget(), {
    kind: "video",
    readyState: "live",
    stop: vi.fn(),
  });
  video.stop.mockImplementation(() => {
    video.readyState = "ended";
  });
  const audio = { kind: "audio", stop: vi.fn() };
  const stream = {
    getVideoTracks: () => [video],
    getAudioTracks: () => [audio],
    getTracks: () => [video, audio],
    removeTrack: vi.fn(),
  } as unknown as MediaStream;
  return { stream, video, audio };
}
function setup(picker: () => Promise<MediaStream>) {
  Object.defineProperty(navigator, "mediaDevices", {
    configurable: true,
    value: { getDisplayMedia: vi.fn(picker) },
  });
  const options = {
    onStart: vi.fn(async () => true),
    onStop: vi.fn(),
    setError: vi.fn(),
  };
  const hook = renderHook(() => useDisplayCapture(options));
  return { ...hook, options };
}
afterEach(cleanup);

test("requests display video, owns cleanup, and handles browser-ended capture once", async () => {
  const source = display();
  const { result, options } = setup(async () => source.stream);
  await act(() => result.current.start());
  expect(navigator.mediaDevices.getDisplayMedia).toHaveBeenCalledWith({
    video: true,
    audio: false,
  });
  expect(source.audio.stop).toHaveBeenCalledOnce();
  expect(source.stream.removeTrack).toHaveBeenCalledWith(source.audio);
  expect(result.current.active).toBe(true);
  act(() => source.video.dispatchEvent(new Event("ended")));
  expect(result.current.active).toBe(false);
  expect(options.onStop).toHaveBeenCalledOnce();
  act(() => source.video.dispatchEvent(new Event("ended")));
  expect(options.onStop).toHaveBeenCalledOnce();
});

test("picker cancellation preserves the current source", async () => {
  const source = display();
  const { result, options } = setup(async () => source.stream);
  await act(() => result.current.start());
  vi.mocked(navigator.mediaDevices.getDisplayMedia).mockRejectedValueOnce(
    new DOMException("cancel", "NotAllowedError"),
  );
  await act(() => result.current.start());
  expect(result.current.active).toBe(true);
  expect(options.onStart).toHaveBeenCalledOnce();
  expect(options.onStop).not.toHaveBeenCalled();
  expect(source.video.stop).not.toHaveBeenCalled();
  expect(options.setError).toHaveBeenLastCalledWith("");
});

test("replacement stops old capture and ignores its stale ended event", async () => {
  const first = display(),
    second = display();
  const { result, options } = setup(async () => first.stream);
  await act(() => result.current.start());
  vi.mocked(navigator.mediaDevices.getDisplayMedia).mockResolvedValueOnce(
    second.stream,
  );
  await act(() => result.current.start());
  expect(first.video.stop).toHaveBeenCalledOnce();
  act(() => first.video.dispatchEvent(new Event("ended")));
  expect(result.current.active).toBe(true);
  expect(options.onStop).not.toHaveBeenCalled();
  act(() => result.current.stop());
  expect(second.video.stop).toHaveBeenCalledOnce();
});

test("pending picker results cannot resurrect capture after cancel or unmount", async () => {
  const source = display();
  let resolve!: (stream: MediaStream) => void;
  const { result, options, unmount } = setup(
    () =>
      new Promise((done) => {
        resolve = done;
      }),
  );
  let pending!: Promise<void>;
  act(() => {
    pending = result.current.start();
  });
  expect(result.current.selecting).toBe(true);
  act(() => result.current.release());
  unmount();
  await act(async () => {
    resolve(source.stream);
    await pending;
  });
  expect(source.video.stop).toHaveBeenCalledOnce();
  expect(options.onStart).not.toHaveBeenCalled();
});

test("unmount stops display video but leaves pipeline-added microphone tracks alone", async () => {
  const source = display();
  const { result, unmount } = setup(async () => source.stream);
  await act(() => result.current.start());
  source.audio.stop.mockClear();
  unmount();
  expect(source.video.stop).toHaveBeenCalledOnce();
  expect(source.audio.stop).not.toHaveBeenCalled();
});

test("capture failures are visible and unsupported browsers do not acquire media", async () => {
  const { result, options } = setup(async () => {
    throw new DOMException("blocked", "NotReadableError");
  });
  await act(() => result.current.start());
  expect(options.setError).toHaveBeenLastCalledWith(
    expect.stringContaining("Failed to share screen"),
  );
  expect(result.current.active).toBe(false);
});

test("unsupported browser reports why sharing is unavailable", async () => {
  Object.defineProperty(navigator, "mediaDevices", {
    configurable: true,
    value: {},
  });
  const options = {
    onStart: vi.fn(async () => true),
    onStop: vi.fn(),
    setError: vi.fn(),
  };
  const { result } = renderHook(() => useDisplayCapture(options));
  expect(result.current.supported).toBe(false);
  await act(() => result.current.start());
  expect(options.onStart).not.toHaveBeenCalled();
  expect(options.setError).toHaveBeenCalledWith(
    expect.stringContaining("unavailable"),
  );
});

test("invalid replacement releases rejected tracks and preserves active capture", async () => {
  const source = display(),
    invalid = display();
  const { result, options } = setup(async () => source.stream);
  await act(() => result.current.start());
  invalid.video.readyState = "ended";
  vi.mocked(navigator.mediaDevices.getDisplayMedia).mockResolvedValueOnce(
    invalid.stream,
  );
  await act(() => result.current.start());
  expect(invalid.video.stop).toHaveBeenCalledOnce();
  expect(invalid.audio.stop).toHaveBeenCalledOnce();
  expect(result.current.active).toBe(true);
  expect(source.video.stop).not.toHaveBeenCalled();
  expect(options.onStop).not.toHaveBeenCalled();
});

test("a failed replacement cleans up both display owners and restores the prior source", async () => {
  const source = display(),
    replacement = display();
  const { result, options } = setup(async () => source.stream);
  await act(() => result.current.start());
  vi.mocked(navigator.mediaDevices.getDisplayMedia).mockResolvedValueOnce(
    replacement.stream,
  );
  options.onStart.mockRejectedValueOnce(new Error("renderer failed"));
  await act(() => result.current.start());
  expect(source.video.stop).toHaveBeenCalledOnce();
  expect(replacement.video.stop).toHaveBeenCalledOnce();
  expect(result.current.active).toBe(false);
  expect(result.current.selecting).toBe(false);
  expect(options.onStop).toHaveBeenCalledOnce();
});
