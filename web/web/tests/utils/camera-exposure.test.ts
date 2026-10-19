import { expect, test, vi } from "vite-plus/test";
import {
  setFastExposure,
  supportsFastExposure,
} from "../../src/utils/cameraExposure";

function camera() {
  return {
    readyState: "live",
    getCapabilities: () => ({
      frameRate: { max: 60 },
      exposureMode: ["continuous", "manual"],
      exposureTime: { min: 10, max: 10000, step: 10 },
    }),
    getSettings: () => ({
      exposureMode: "continuous",
      exposureTime: 300,
      frameRate: 60,
    }),
    getConstraints: () => ({
      frameRate: { ideal: 60 },
      deviceId: { exact: "camera" },
    }),
    applyConstraints: vi.fn().mockResolvedValue(undefined),
  } as unknown as MediaStreamTrack;
}
test("fast exposure fits the frame interval and restores automatic exposure", async () => {
  const track = camera();
  expect(supportsFastExposure(track)).toBe(true);
  await setFastExposure(track, true);
  expect(track.applyConstraints).toHaveBeenLastCalledWith({
    frameRate: { ideal: 60 },
    deviceId: { exact: "camera" },
    advanced: [{ exposureMode: "manual", exposureTime: 160 }],
  });
  await setFastExposure(track, false);
  expect(track.applyConstraints).toHaveBeenLastCalledWith({
    frameRate: { ideal: 60 },
    deviceId: { exact: "camera" },
    advanced: [{ exposureMode: "continuous", exposureTime: 300 }],
  });
});
test("quick toggles serialize camera changes and restore the original settings", async () => {
  const track = camera();
  await Promise.all([
    setFastExposure(track, true),
    setFastExposure(track, false),
    setFastExposure(track, true),
    setFastExposure(track, false),
  ]);
  expect(track.applyConstraints).toHaveBeenCalledTimes(4);
  expect(track.applyConstraints).toHaveBeenLastCalledWith(
    expect.objectContaining({
      advanced: [{ exposureMode: "continuous", exposureTime: 300 }],
    }),
  );
});
test("unsupported and stopped cameras are left alone", async () => {
  const track = camera();
  Object.assign(track, { getCapabilities: () => ({}) });
  expect(supportsFastExposure(track)).toBe(false);
  await setFastExposure(track, true);
  Object.assign(track, { readyState: "ended" });
  await setFastExposure(track, false);
  expect(track.applyConstraints).not.toHaveBeenCalled();
});
