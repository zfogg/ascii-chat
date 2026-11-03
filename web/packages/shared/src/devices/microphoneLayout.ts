import {
  getMediaDevicePreferences,
  MEDIA_DEVICE_PREFERENCES_CHANGED,
} from "./mediaDevicePreferences";

export interface MicrophoneLayout {
  channels: 1 | 2;
  firstChannel: number;
}
const key = "ascii-chat.microphone-layout";
let volatileLayout: MicrophoneLayout | null = null;

export function getMicrophoneLayout(): MicrophoneLayout {
  if (volatileLayout) return { ...volatileLayout };
  try {
    const value = JSON.parse(localStorage.getItem(key) || "{}");
    return {
      channels: value.channels === 2 ? 2 : 1,
      firstChannel:
        Number.isInteger(value.firstChannel) &&
        value.firstChannel >= 0 &&
        value.firstChannel <= 30
          ? value.firstChannel
          : 0,
    };
  } catch {
    return { channels: 1, firstChannel: 0 };
  }
}

export function saveMicrophoneLayout(layout: MicrophoneLayout): void {
  try {
    localStorage.setItem(key, JSON.stringify(layout));
    volatileLayout = null;
  } catch {
    volatileLayout = { ...layout };
  }
  window.dispatchEvent(
    new CustomEvent(MEDIA_DEVICE_PREFERENCES_CHANGED, {
      detail: {
        preferences: getMediaDevicePreferences(),
        changedKeys: ["microphoneId"],
      },
    }),
  );
}
