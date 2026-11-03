import { afterEach, beforeEach, expect, it, vi } from "vite-plus/test";
import {
  getMicrophoneLayout,
  saveMicrophoneLayout,
} from "@ascii-chat/shared/devices/microphoneLayout";
import { MEDIA_DEVICE_PREFERENCES_CHANGED } from "@ascii-chat/shared/devices/mediaDevicePreferences";

beforeEach(() => {
  localStorage.clear();
  saveMicrophoneLayout({ channels: 1, firstChannel: 0 });
});
afterEach(() => {
  vi.restoreAllMocks();
  saveMicrophoneLayout({ channels: 1, firstChannel: 0 });
  localStorage.clear();
});

it("defaults to the first mono speech input and validates persisted channel selection", () => {
  expect(getMicrophoneLayout()).toEqual({ channels: 1, firstChannel: 0 });
  localStorage.setItem(
    "ascii-chat.microphone-layout",
    JSON.stringify({ channels: 8, firstChannel: -2 }),
  );
  expect(getMicrophoneLayout()).toEqual({ channels: 1, firstChannel: 0 });
});
it("persists an interface pair and asks active capture owners to reopen their microphone", () => {
  const changed = vi.fn();
  window.addEventListener(MEDIA_DEVICE_PREFERENCES_CHANGED, changed);
  saveMicrophoneLayout({ channels: 2, firstChannel: 2 });
  expect(getMicrophoneLayout()).toEqual({ channels: 2, firstChannel: 2 });
  expect(changed).toHaveBeenCalledOnce();
  expect(changed.mock.calls[0]![0].detail.changedKeys).toContain(
    "microphoneId",
  );
  window.removeEventListener(MEDIA_DEVICE_PREFERENCES_CHANGED, changed);
});
it("applies selection for the current tab when browser storage refuses writes", () => {
  vi.spyOn(Storage.prototype, "setItem").mockImplementation(() => {
    throw new Error("Storage blocked");
  });
  saveMicrophoneLayout({ channels: 2, firstChannel: 4 });
  expect(getMicrophoneLayout()).toEqual({ channels: 2, firstChannel: 4 });
});
