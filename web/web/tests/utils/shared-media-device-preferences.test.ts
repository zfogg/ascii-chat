import { beforeEach, expect, test, vi } from "vite-plus/test";
import {
  getStoredMediaDevicePreferences,
  saveMediaDevicePreferences,
  cameraConstraints,
  microphoneConstraints,
  applyAudioOutputDevice,
  MEDIA_DEVICE_PREFERENCES_CHANGED,
} from "@ascii-chat/shared/devices/mediaDevicePreferences";
beforeEach(() => {
  localStorage.clear();
  window.history.replaceState(
    {},
    "",
    "/docs/display?cameraId=url-camera#face-follow",
  );
});
test("validates stored values and ignores query state", () => {
  localStorage.setItem(
    "ascii-chat.media-devices",
    JSON.stringify({
      cameraId: 42,
      microphoneId: "saved-mic",
      speakerId: null,
    }),
  );
  expect(getStoredMediaDevicePreferences()).toEqual({
    cameraId: "",
    microphoneId: "saved-mic",
    speakerId: "",
  });
  localStorage.setItem("ascii-chat.media-devices", "invalid-json");
  expect(getStoredMediaDevicePreferences()).toEqual({
    cameraId: "",
    microphoneId: "",
    speakerId: "",
  });
});
test("saves selections and reports only changed devices without rewriting URL", () => {
  const listener = vi.fn();
  window.addEventListener(MEDIA_DEVICE_PREFERENCES_CHANGED, listener);
  const url = location.href;
  try {
    saveMediaDevicePreferences({
      cameraId: "camera-one",
      microphoneId: "",
      speakerId: "",
    });
    expect(listener.mock.calls[0]![0].detail.changedKeys).toEqual(["cameraId"]);
    expect(getStoredMediaDevicePreferences().cameraId).toBe("camera-one");
    saveMediaDevicePreferences({
      cameraId: "camera-one",
      microphoneId: "mic-one",
      speakerId: "",
    });
    expect(listener.mock.calls[1]![0].detail.changedKeys).toEqual([
      "microphoneId",
    ]);
    expect(location.href).toBe(url);
  } finally {
    window.removeEventListener(MEDIA_DEVICE_PREFERENCES_CHANGED, listener);
  }
});
test("maps selections into exact capture constraints and preserves browser defaults", () => {
  expect(cameraConstraints("camera-one", { width: { ideal: 640 } })).toEqual({
    width: { ideal: 640 },
    deviceId: { exact: "camera-one" },
  });
  expect(cameraConstraints("")).toEqual({});
  expect(microphoneConstraints("")).toBe(true);
  expect(microphoneConstraints("mic-one")).toEqual({
    deviceId: { exact: "mic-one" },
  });
});
test("routes output and reports unsupported speaker selection", async () => {
  const setSinkId = vi.fn().mockResolvedValue(undefined);
  await applyAudioOutputDevice(
    { setSinkId } as unknown as AudioContext,
    "speaker-one",
  );
  expect(setSinkId).toHaveBeenCalledWith("speaker-one");
  await expect(
    applyAudioOutputDevice({} as AudioContext, ""),
  ).resolves.toBeUndefined();
  await expect(
    applyAudioOutputDevice({} as AudioContext, "speaker-one"),
  ).rejects.toThrow("not supported");
});
