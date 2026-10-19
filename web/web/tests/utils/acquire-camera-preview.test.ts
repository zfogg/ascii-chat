import { afterEach, expect, test, vi } from "vite-plus/test";
import { acquireCameraPreview } from "../../src/utils/acquireCameraPreview";
import { reportCameraTrack } from "../../../packages/shared/src/capture/cameraRate";

afterEach(() => {
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});
function camera() {
  const clone = { stop: vi.fn() };
  const active = {
    readyState: "live",
    label: "EMEET",
    getSettings: () => ({ deviceId: "active", frameRate: 60 }),
    getCapabilities: () => ({ frameRate: { max: 60 } }),
    clone: vi.fn(() => clone),
    stop: vi.fn(),
  };
  reportCameraTrack(active as unknown as MediaStreamTrack);
  return { active, clone };
}
test("device preview clones the active camera without requesting another device", async () => {
  const { active, clone } = camera();
  const getUserMedia = vi.fn();
  vi.stubGlobal("navigator", { mediaDevices: { getUserMedia } });
  vi.stubGlobal("MediaStream", class { constructor(public tracks: unknown[]) {} });
  const stream = await acquireCameraPreview("active", new AbortController().signal);
  expect((stream as unknown as {tracks: unknown[]}).tracks).toEqual([clone]);
  expect(getUserMedia).not.toHaveBeenCalled();
  expect(active.stop).not.toHaveBeenCalled();
});

test("a different preview camera requests high-rate capture without audio", async () => {
  const { active } = camera();
  const stream = { getTracks: () => [] };
  const getUserMedia = vi.fn().mockResolvedValue(stream);
  vi.stubGlobal("navigator", { mediaDevices: { getUserMedia } });
  await acquireCameraPreview("other", new AbortController().signal);
  expect(active.clone).not.toHaveBeenCalled();
  expect(getUserMedia).toHaveBeenCalledWith({
    video: { width: { ideal: 1280 }, height: { ideal: 720 }, deviceId: { exact: "other" }, frameRate: { ideal: 60 } }, audio: false,
  });
});

test("an aborted preview does not clone the live camera", async () => {
  const { active } = camera();
  const owner = new AbortController();owner.abort();
  await expect(acquireCameraPreview("active", owner.signal)).rejects.toThrow();
  expect(active.clone).not.toHaveBeenCalled();
});
