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
test("device preview reuses the active camera without renegotiating its capture rate", async () => {
  const { active, clone } = camera();
  const stream = { addTrack: vi.fn() };
  const getUserMedia = vi.fn().mockResolvedValue(stream);
  vi.stubGlobal("navigator", { mediaDevices: { getUserMedia } });
  expect(await acquireCameraPreview({ cameraId: "active" })).toBe(stream);
  expect(getUserMedia).toHaveBeenCalledWith({ video: false, audio: true });
  expect(stream.addTrack).toHaveBeenCalledWith(clone);
  expect(active.stop).not.toHaveBeenCalled();
});
test("a different preview camera requests high-rate capture", async () => {
  const { active } = camera();
  const getUserMedia = vi.fn().mockResolvedValue({});
  vi.stubGlobal("navigator", { mediaDevices: { getUserMedia } });
  await acquireCameraPreview({ cameraId: "other" });
  expect(active.clone).not.toHaveBeenCalled();
  expect(getUserMedia).toHaveBeenCalledWith({
    video: { deviceId: { exact: "other" }, frameRate: { ideal: 60 } },
    audio: true,
  });
});
test("a failed microphone request releases only the preview clone", async () => {
  const { active, clone } = camera();
  vi.stubGlobal("navigator", {
    mediaDevices: {
      getUserMedia: vi
        .fn()
        .mockRejectedValue(new Error("microphone unavailable")),
    },
  });
  await expect(acquireCameraPreview({})).rejects.toThrow(
    "microphone unavailable",
  );
  expect(clone.stop).toHaveBeenCalledOnce();
  expect(active.stop).not.toHaveBeenCalled();
});
