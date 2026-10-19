import { expect, test, vi } from "vite-plus/test";
import {
  setFastExposure,
  supportsFastExposure,
} from "../../src/utils/cameraExposure";

function camera() {
  const settings = {
    exposureMode: "continuous",
    exposureTime: 300,
    frameRate: 60,
  };
  return {
    readyState: "live",
    getCapabilities: () => ({
      frameRate: { max: 60 },
      exposureMode: ["continuous", "manual"],
      exposureTime: { min: 10, max: 10000, step: 10 },
    }),
    getSettings: () => ({ ...settings }),
    getConstraints: () => ({
      frameRate: { ideal: 60 },
      deviceId: { exact: "camera" },
    }),
    applyConstraints: vi.fn(async (constraints: MediaTrackConstraints) => {
      Object.assign(settings, constraints.advanced?.[0]);
    }),
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
    advanced: [{ exposureMode: "continuous" }],
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
      advanced: [{ exposureMode: "continuous" }],
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

test("retries when a UVC driver rounds exposure above the frame interval", async () => {
  const track = camera();
  let exposureTime = 300;
  Object.assign(track, {
    getSettings: () => ({
      exposureMode: "manual",
      exposureTime,
      frameRate: 60,
    }),
    applyConstraints: vi.fn(async (constraints: MediaTrackConstraints) => {
      const requested = (constraints.advanced?.[0] as { exposureTime: number })
        .exposureTime;
      exposureTime = requested > 150 ? 312.5 : 156.25;
    }),
  });
  await setFastExposure(track, true);
  expect(track.applyConstraints).toHaveBeenCalledTimes(2);
  expect(exposureTime).toBeLessThanOrEqual(10000 / 60);
});

test("off restores automatic exposure even on a newly acquired manual track", async () => {
  const track = camera();
  Object.assign(track, {
    getSettings: () => ({
      exposureMode: "manual",
      exposureTime: 156.25,
      frameRate: 60,
    }),
  });
  await setFastExposure(track, false);
  expect(track.applyConstraints).toHaveBeenLastCalledWith(
    expect.objectContaining({ advanced: [{ exposureMode: "continuous" }] }),
  );
});
test("disabling inherited manual exposure cannot restore the fast manual setting", async () => {
  const track = camera();
  Object.assign(track, {
    getSettings: () => ({
      exposureMode: "manual",
      exposureTime: 156.25,
      frameRate: 60,
    }),
  });
  await setFastExposure(track, true);
  await setFastExposure(track, false);
  expect(track.applyConstraints).toHaveBeenLastCalledWith(
    expect.objectContaining({ advanced: [{ exposureMode: "continuous" }] }),
  );
});
