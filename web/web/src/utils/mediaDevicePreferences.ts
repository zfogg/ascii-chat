import { readUrlValue, writeUrlValue } from "./urlState";
import {
  getStoredMediaDevicePreferences,
  saveMediaDevicePreferences as saveStored,
  type MediaDevicePreferences,
} from "@ascii-chat/shared/devices/mediaDevicePreferences";
export * from "@ascii-chat/shared/devices/mediaDevicePreferences";
export function getMediaDevicePreferences(): MediaDevicePreferences {
  return readUrlValue("mediaDevices", getStoredMediaDevicePreferences());
}
export function saveMediaDevicePreferences(
  preferences: MediaDevicePreferences,
  previous = getMediaDevicePreferences(),
): void {
  writeUrlValue(
    "mediaDevices",
    preferences,
    false,
    getStoredMediaDevicePreferences(),
  );
  saveStored(preferences, previous);
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
  if (selectedCameraId !== undefined) return selectedCameraId;
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
    return storedCameraId;
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
