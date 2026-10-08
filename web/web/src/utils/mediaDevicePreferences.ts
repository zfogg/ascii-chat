const STORAGE_KEY = "ascii-chat.media-devices";

export interface MediaDevicePreferences {
  cameraId: string;
  microphoneId: string;
  speakerId: string;
}

export type MediaDevicePreferenceKey = keyof MediaDevicePreferences;

export interface MediaDevicePreferencesChange {
  preferences: MediaDevicePreferences;
  changedKeys: MediaDevicePreferenceKey[];
}

export const MEDIA_DEVICE_PREFERENCES_CHANGED =
  "ascii-chat:media-devices-changed";

const EMPTY_PREFERENCES: MediaDevicePreferences = {
  cameraId: "",
  microphoneId: "",
  speakerId: "",
};

export function getMediaDevicePreferences(): MediaDevicePreferences {
  if (typeof window === "undefined") return EMPTY_PREFERENCES;

  try {
    const stored = window.localStorage.getItem(STORAGE_KEY);
    if (!stored) return EMPTY_PREFERENCES;
    const parsed: unknown = JSON.parse(stored);
    if (!parsed || typeof parsed !== "object") return EMPTY_PREFERENCES;
    const preferences = parsed as Partial<MediaDevicePreferences>;
    return {
      cameraId:
        typeof preferences.cameraId === "string" ? preferences.cameraId : "",
      microphoneId:
        typeof preferences.microphoneId === "string"
          ? preferences.microphoneId
          : "",
      speakerId:
        typeof preferences.speakerId === "string" ? preferences.speakerId : "",
    };
  } catch {
    return EMPTY_PREFERENCES;
  }
}

export function saveMediaDevicePreferences(
  preferences: MediaDevicePreferences,
): void {
  if (typeof window === "undefined") return;
  const previous = getMediaDevicePreferences();
  window.localStorage.setItem(STORAGE_KEY, JSON.stringify(preferences));
  const changedKeys = (
    Object.keys(preferences) as MediaDevicePreferenceKey[]
  ).filter((key) => previous[key] !== preferences[key]);
  window.dispatchEvent(
    new CustomEvent<MediaDevicePreferencesChange>(
      MEDIA_DEVICE_PREFERENCES_CHANGED,
      { detail: { preferences, changedKeys } },
    ),
  );
}
