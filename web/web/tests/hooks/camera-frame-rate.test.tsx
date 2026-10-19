import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, expect, test, vi } from "vite-plus/test";
import { useState } from "react";
import {
  CameraCadence,
  reportCameraFrame,
  reportCameraTrack,
  cameraAcquisitionFps,
  cameraNeedsFrameLimit,
} from "../../../packages/shared/src/capture/cameraRate";
import {
  effectiveCameraFps,
  useCameraFrameRate,
} from "../../src/hooks/useCameraFrameRate";
import { DEFAULT_SETTINGS } from "../../src/utils/defaultSettings";

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  history.replaceState({}, "", "/");
});

test.each([20, 25, 30, 100 / 3, 59.94, 60, 120])(
  "measures %s FPS independently of arrival jitter",
  (rate) => {
    const cadence = new CameraCadence();
    let measured: number | null = null;
    for (let frame = 0; frame < rate * 5; frame++)
      measured =
        cadence.sample(
          (frame * 1000) / rate,
          (frame * 1000) / rate + (frame % 2 ? 0.7 : -0.7),
        ) ?? measured;
    expect(measured).toBe(Math.round(rate));
  },
);

test("duplicates and a long host stall do not lower the measured camera ceiling", () => {
  const cadence = new CameraCadence();
  let result: number | null = null;
  for (let i = 0; i < 300; i++) {
    result = cadence.sample((i * 1000) / 60, (i * 1000) / 60) ?? result;
    expect(cadence.sample((i * 1000) / 60, (i * 1000) / 60 + 1)).toBeNull();
  }
  expect(result).toBe(60);
  expect(cadence.sample(8000, 8000)).toBeNull();
});

test("alternating USB frame intervals report delivered FPS rather than median interval FPS", () => {
  const cadence = new CameraCadence();
  let timestamp = 0;
  let measured: number | null = null;
  for (let frame = 0; frame < 300; frame++) {
    timestamp += frame % 2 ? 25 : 35;
    measured = cadence.sample(timestamp, timestamp) ?? measured;
  }
  expect(measured).toBe(33);
});

test("Max follows decreases and increases while a manual limit survives both", () => {
  expect(effectiveCameraFps("auto", 20)).toBe(20);
  expect(effectiveCameraFps("auto", 60)).toBe(60);
  expect(effectiveCameraFps("30", 20)).toBe(20);
  expect(effectiveCameraFps("30", 60)).toBe(30);
});

test("configured camera modes clamp and recover the setting while slower delivery does not", async () => {
  let now = 0;
  vi.spyOn(performance, "now").mockImplementation(() => now);
  let configured = 60;
  const track = {
    label: "60 FPS camera",
    getCapabilities: () => ({ frameRate: { max: 60 } }),
    getSettings: () => ({ frameRate: configured }),
  } as unknown as MediaStreamTrack;
  reportCameraTrack(track);
  const hook = renderHook(() => {
    const [settings, setSettings] = useState(DEFAULT_SETTINGS);
    const control = useCameraFrameRate(settings, setSettings, true);
    return { settings, control };
  });
  const deliver = async (fps: number) => {
    for (let frame = 0; frame < fps * 7; frame++) {
      now += 1000 / fps;
      await act(async () => reportCameraFrame(now));
    }
  };
  await deliver(33);
  expect(hook.result.current.control.rate?.observed).toBe(33);
  expect(hook.result.current.settings.targetFps).toBe(60);
  configured = 20;
  await deliver(20);
  expect(hook.result.current.settings.targetFps).toBe(20);
  expect(hook.result.current.control.maximum).toBe(20);
  expect(cameraAcquisitionFps(track)).toBe(60);
  configured = 60;
  await deliver(60);
  expect(hook.result.current.settings.targetFps).toBe(60);
  await act(async () => hook.result.current.control.select(15));
  configured = 25;
  await deliver(25);
  expect(hook.result.current.settings.targetFps).toBe(15);
  await act(async () => hook.result.current.control.select(25));
  configured = 60;
  await deliver(60);
  expect(hook.result.current.settings.targetFps).toBe(60);
});

test("Max accepts camera delivery jitter without resampling; lower choices still throttle", () => {
  reportCameraTrack({
    label: "jittery camera",
    getCapabilities: () => ({ frameRate: { max: 60 } }),
    getSettings: () => ({ frameRate: 60 }),
  } as unknown as MediaStreamTrack);
  expect(cameraNeedsFrameLimit(60)).toBe(false);
  expect(cameraNeedsFrameLimit(1000 / (1000 / 60))).toBe(false);
  expect(cameraNeedsFrameLimit(30)).toBe(true);
});
