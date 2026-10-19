import { readUrlValue, writeUrlValue } from "./urlState";
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

export function getStoredMediaDevicePreferences(): MediaDevicePreferences {
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
  writeUrlValue(
    "mediaDevices",
    preferences,
    false,
    getStoredMediaDevicePreferences(),
  );
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

export function getMediaDevicePreferences(): MediaDevicePreferences {
  return readUrlValue("mediaDevices", getStoredMediaDevicePreferences());
}

/** Resolve a camera without opening it or probing other devices for availability. */
export async function resolveCameraDeviceId({
  selectedCameraId,
  cameras,
  search = window.location.search,
}: {
  selectedCameraId?: string | undefined;
  cameras?: readonly MediaDeviceInfo[];
  search?: string;
} = {}): Promise<string> {
  if (selectedCameraId) return selectedCameraId;
  const params = new URLSearchParams(search);
  const queryCameraId = params.get("cameraId");
  if (queryCameraId) return queryCameraId;
  if (!params.has("cameraId") && params.has("mediaDevices")) {
    try {
      const legacy: unknown = JSON.parse(params.get("mediaDevices")!);
      if (
        legacy &&
        typeof legacy === "object" &&
        "cameraId" in legacy &&
        typeof legacy.cameraId === "string" &&
        legacy.cameraId
      )
        return legacy.cameraId;
    } catch {
      // Malformed older URLs follow the scalar query and saved-device rules.
    }
  }

  const indexValue = params.get("videoDeviceIndex");
  const cameraIndex = indexValue === null ? null : Number(indexValue);
  if (
    cameraIndex !== null &&
    (!Number.isInteger(cameraIndex) || cameraIndex < 0 || !indexValue?.trim())
  )
    throw new Error("videoDeviceIndex must be a non-negative integer");

  if (cameraIndex === null) {
    const storedCameraId = getStoredMediaDevicePreferences().cameraId;
    if (storedCameraId) return storedCameraId;
  }

  const choices =
    cameras ??
    (await navigator.mediaDevices.enumerateDevices()).filter(
      (device) => device.kind === "videoinput",
    );
  if (cameraIndex !== null && !choices[cameraIndex])
    throw new Error(
      `Camera index ${cameraIndex} is unavailable (${choices.length} camera(s) listed)`,
    );
  // Before permission, browsers may expose only one anonymous default camera.
  return choices[cameraIndex ?? 0]?.deviceId || "";
}
