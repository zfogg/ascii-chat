import { useCallback, useRef } from "react";
import { useUrlState } from "../hooks/useUrlState";
import { DeviceSetupModal as SharedDeviceSetupModal } from "@ascii-chat/shared/components/DeviceSetupModal";
import { useVideoEncodings } from "../hooks/useVideoEncodings";
import { cameraExposurePreference } from "../hooks/useCameraExposure";
import {
  getMediaDevicePreferences,
  getStoredMediaDevicePreferences,
  saveMediaDevicePreferences,
  resolveCameraDeviceId,
} from "../utils/mediaDevicePreferences";
function clearCameraIndex() {
  const url = new URL(window.location.href);
  url.searchParams.delete("videoDeviceIndex");
  window.history.replaceState(window.history.state, "", url);
}
export function DeviceSetupModal({
  open,
  onClose,
}: {
  open: boolean;
  onClose: () => void;
}) {
  const [preferences, setPreferences] = useUrlState(
    "mediaDevices",
    getStoredMediaDevicePreferences,
  );
  const committed = useRef(getMediaDevicePreferences());
  const cameraSearch = useRef(window.location.search);
  const readPreferences = useCallback(() => {
    committed.current = getMediaDevicePreferences();
    cameraSearch.current = window.location.search;
    return committed.current;
  }, []);
  const savePreferences = useCallback(
    (next: typeof preferences) =>
      saveMediaDevicePreferences(next, committed.current),
    [],
  );
  const resolveCamera = useCallback(
    (options: Parameters<typeof resolveCameraDeviceId>[0]) =>
      resolveCameraDeviceId({ ...options, search: cameraSearch.current }),
    [],
  );
  const videoEncodings = useVideoEncodings();
  return (
    <SharedDeviceSetupModal
      open={open}
      onClose={onClose}
      preferences={preferences}
      onPreferencesChange={setPreferences}
      readPreferences={readPreferences}
      savePreferences={savePreferences}
      resolveCamera={resolveCamera}
      onCameraChange={clearCameraIndex}
      exposurePreference={cameraExposurePreference}
      videoEncodings={videoEncodings}
    />
  );
}
