import { useEffect, useState, useSyncExternalStore } from "react";
import {
  setFastExposure,
  supportsFastExposure,
} from "../devices/cameraExposure";

const event = "ascii-chat-camera-exposure";
const snapshot = () => {
  try {
    return localStorage.getItem(event) === "true";
  } catch {
    return false;
  }
};
export interface CameraExposurePreference {
  snapshot: () => boolean;
  subscribe: (notify: () => void) => () => void;
  select: (enabled: boolean) => void;
}
const subscribe = (notify: () => void) => {
  window.addEventListener(event, notify);
  window.addEventListener("storage", notify);
  return () => {
    window.removeEventListener(event, notify);
    window.removeEventListener("storage", notify);
  };
};

export const storedCameraExposure: CameraExposurePreference = {
  snapshot,
  subscribe,
  select(enabled) {
    try {
      localStorage.setItem(event, String(enabled));
    } catch {
      /* Storage may be disabled. */
    }
    window.dispatchEvent(new Event(event));
  },
};

/** A device debug preference shared by the live capture and device preview. */
export function useCameraExposure(
  track: MediaStreamTrack | null,
  preference = storedCameraExposure,
) {
  const enabled = useSyncExternalStore(
    preference.subscribe,
    preference.snapshot,
    () => false,
  );
  const [failure, setFailure] = useState<{
    track: MediaStreamTrack;
    enabled: boolean;
    message: string;
  } | null>(null);
  const supported = !!track && supportsFastExposure(track);
  useEffect(() => {
    if (!track || !supported) return;
    let current = true;
    void setFastExposure(track, enabled).then(
      () => {
        if (current) setFailure(null);
      },
      (cause: unknown) => {
        if (current)
          setFailure({
            track,
            enabled,
            message: cause instanceof Error ? cause.message : String(cause),
          });
      },
    );
    return () => {
      current = false;
      void setFastExposure(track, false).catch(() => {});
    };
  }, [track, enabled, supported]);
  return {
    enabled,
    supported,
    // A failed request belongs to its track and preference, never the next
    // selected camera or a preview handed back to the live capture.
    error:
      supported && failure?.track === track && failure.enabled === enabled
        ? failure.message
        : "",
    select(value: boolean) {
      preference.select(value);
    },
  };
}
