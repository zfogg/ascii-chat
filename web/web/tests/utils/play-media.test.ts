import { afterEach, expect, test, vi } from "vite-plus/test";
import { playMedia } from "../../src/utils/playMedia";

afterEach(() => {
  vi.restoreAllMocks();
  vi.useRealTimers();
});

test("autoplay denial retains a loaded frame for the Play button", async () => {
  const video = document.createElement("video");
  vi.spyOn(video, "play").mockRejectedValue(
    new DOMException("Gesture required", "NotAllowedError"),
  );
  const pending = playMedia(video, new AbortController().signal);
  video.dispatchEvent(new Event("loadeddata"));
  await expect(pending).resolves.toBeUndefined();
});

test("decode errors reject promptly for FFmpeg fallback", async () => {
  const video = document.createElement("video");
  vi.spyOn(video, "play").mockResolvedValue();
  const pending = playMedia(video, new AbortController().signal);
  video.dispatchEvent(new Event("error"));
  await expect(pending).rejects.toHaveProperty("name", "NotSupportedError");
});

test("cancellation rejects a pending load and removes event listeners", async () => {
  const video = document.createElement("video");
  vi.spyOn(video, "play").mockImplementation(() => new Promise(() => {}));
  const remove = vi.spyOn(video, "removeEventListener");
  const owner = new AbortController();
  const pending = playMedia(video, owner.signal);
  owner.abort();
  await expect(pending).rejects.toHaveProperty("name", "AbortError");
  expect(remove).toHaveBeenCalledWith("loadeddata", expect.any(Function));
  expect(remove).toHaveBeenCalledWith("error", expect.any(Function));
});

test("loaded data does not bypass the deadline when play never finishes", async () => {
  vi.useFakeTimers();
  const video = document.createElement("video");
  vi.spyOn(video, "play").mockImplementation(() => new Promise(() => {}));
  const pending = playMedia(video, new AbortController().signal, 10_000);
  const rejected = expect(pending).rejects.toHaveProperty(
    "name",
    "TimeoutError",
  );
  video.dispatchEvent(new Event("loadeddata"));
  await vi.advanceTimersByTimeAsync(10_000);
  await rejected;
  expect(vi.getTimerCount()).toBe(0);
});

test("a loaded frame does not prevent cancellation of a pending play call", async () => {
  const video = document.createElement("video");
  vi.spyOn(video, "play").mockImplementation(() => new Promise(() => {}));
  const owner = new AbortController();
  const pending = playMedia(video, owner.signal);
  const rejected = expect(pending).rejects.toHaveProperty("name", "AbortError");
  video.dispatchEvent(new Event("loadeddata"));
  owner.abort();
  await rejected;
});
