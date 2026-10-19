import { afterEach, beforeEach, expect, test, vi } from "vite-plus/test";
import { acquireMediaStream } from "../../src/utils/acquireMediaStream";

const getUserMedia = vi.fn();
const query = vi.fn();
const stop = vi.fn();
const stream = { getTracks: () => [{ stop }] } as unknown as MediaStream;

beforeEach(() => {
  vi.useFakeTimers();
  vi.clearAllMocks();
  getUserMedia.mockImplementation(() => new Promise(() => {}));
  query.mockRejectedValue(new Error("Permission API unavailable"));
  vi.stubGlobal("navigator", {
    mediaDevices: { getUserMedia },
    permissions: { query },
  });
});

afterEach(() => {
  vi.useRealTimers();
  vi.unstubAllGlobals();
});

test("successful acquisition clears startup timers without stopping the owned stream", async () => {
  getUserMedia.mockResolvedValue(stream);
  await expect(
    acquireMediaStream({ video: true }, new AbortController().signal),
  ).resolves.toBe(stream);
  expect(stop).not.toHaveBeenCalled();
  expect(vi.getTimerCount()).toBe(0);
});

test("cancellation releases a stream returned after its preview was abandoned", async () => {
  let finish!: (stream: MediaStream) => void;
  getUserMedia.mockImplementation(
    () =>
      new Promise<MediaStream>((resolve) => {
        finish = resolve;
      }),
  );
  const owner = new AbortController();
  const pending = acquireMediaStream({ video: true }, owner.signal);
  const rejected = expect(pending).rejects.toHaveProperty("name", "AbortError");
  owner.abort();
  await rejected;
  finish(stream);
  await Promise.resolve();
  expect(stop).toHaveBeenCalledOnce();
  expect(vi.getTimerCount()).toBe(0);
});

test("acquisition eventually times out and stops a device that starts afterward", async () => {
  let finish!: (stream: MediaStream) => void;
  getUserMedia.mockImplementation(
    () =>
      new Promise<MediaStream>((resolve) => {
        finish = resolve;
      }),
  );
  const pending = acquireMediaStream(
    { video: true },
    new AbortController().signal,
  );
  const rejected = expect(pending).rejects.toHaveProperty("name", "AbortError");
  await vi.advanceTimersByTimeAsync(30_000);
  await rejected;
  finish(stream);
  await Promise.resolve();
  expect(stop).toHaveBeenCalledOnce();
  expect(vi.getTimerCount()).toBe(0);
});

test("a granted camera permission bounds device startup to ten seconds", async () => {
  const permission = Object.assign(new EventTarget(), { state: "granted" });
  query.mockResolvedValue(permission);
  const pending = acquireMediaStream(
    { video: true },
    new AbortController().signal,
  );
  const rejected = expect(pending).rejects.toHaveProperty("name", "AbortError");
  await vi.advanceTimersByTimeAsync(10_000);
  await rejected;
  expect(query).toHaveBeenCalledWith({ name: "camera" });
  expect(vi.getTimerCount()).toBe(0);
});

test("granting permission starts a hardware deadline and removes permission listeners afterward", async () => {
  const permission = Object.assign(new EventTarget(), { state: "prompt" });
  const remove = vi.spyOn(permission, "removeEventListener");
  query.mockResolvedValue(permission);
  const pending = acquireMediaStream(
    { video: true },
    new AbortController().signal,
  );
  const rejected = expect(pending).rejects.toHaveProperty("name", "AbortError");
  await vi.advanceTimersByTimeAsync(15_000);
  permission.state = "granted";
  permission.dispatchEvent(new Event("change"));
  await vi.advanceTimersByTimeAsync(10_000);
  await rejected;
  expect(remove).toHaveBeenCalledWith("change", expect.any(Function));
});
