import { useEffect, useState, useSyncExternalStore } from "react";
import { readUrlValue, writeUrlValue } from "../utils/urlState";
import { setFastExposure, supportsFastExposure } from "../utils/cameraExposure";

const event = "ascii-chat-camera-exposure";
const snapshot = () => readUrlValue("fastCameraExposure", false);
const subscribe = (notify: () => void) => {
  window.addEventListener(event, notify);
  window.addEventListener("popstate", notify);
  return () => {
    window.removeEventListener(event, notify);
    window.removeEventListener("popstate", notify);
  };
};

/** A device debug preference shared by the live capture and device preview. */
export function useCameraExposure(track: MediaStreamTrack | null) {
  const enabled = useSyncExternalStore(subscribe, snapshot);
  const [error, setError] = useState("");
  const supported = !!track && supportsFastExposure(track);
  useEffect(() => {
    if (!track || !supported) return;
    let current = true;
    void setFastExposure(track, enabled).then(
      () => {
        if (current) setError("");
      },
      (cause: unknown) => {
        if (current)
          setError(cause instanceof Error ? cause.message : String(cause));
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
    error,
    select(value: boolean) {
      writeUrlValue("fastCameraExposure", value, false, false);
      window.dispatchEvent(new Event(event));
    },
  };
}
