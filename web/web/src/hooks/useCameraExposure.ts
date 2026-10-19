import {
  useCameraExposure as useSharedCameraExposure,
  type CameraExposurePreference,
} from "@ascii-chat/shared/hooks/useCameraExposure";
import { readUrlValue, writeUrlValue } from "../utils/urlState";
const event = "ascii-chat-camera-exposure";
export const cameraExposurePreference: CameraExposurePreference = {
  snapshot: () => readUrlValue("fastCameraExposure", false),
  subscribe(notify) {
    window.addEventListener(event, notify);
    window.addEventListener("popstate", notify);
    return () => {
      window.removeEventListener(event, notify);
      window.removeEventListener("popstate", notify);
    };
  },
  select(value) {
    writeUrlValue("fastCameraExposure", value, false, false);
    window.dispatchEvent(new Event(event));
  },
};
export function useCameraExposure(track: MediaStreamTrack | null) {
  return useSharedCameraExposure(track, cameraExposurePreference);
}
