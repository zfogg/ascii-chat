import {
  useCallback,
  useEffect,
  useRef,
  useState,
  useSyncExternalStore,
} from "react";
import { cameraRateStore } from "../../../packages/shared/src/capture/cameraRate";
import type { BinarySettingsConfig } from "../components";
import { useUrlState } from "./useUrlState";
import { setFastExposure, supportsFastExposure } from "../utils/cameraExposure";

export function effectiveCameraFps(
  preference: string,
  maximum: number,
): number {
  const selected = Number(preference);
  return preference === "auto" || !Number.isFinite(selected) || selected < 1
    ? maximum
    : Math.min(selected, maximum);
}

export function useCameraFrameRate(
  settings: BinarySettingsConfig,
  setSettings: (value: BinarySettingsConfig) => void,
  enabled: boolean,
) {
  const rate = useSyncExternalStore(
    cameraRateStore.subscribe,
    cameraRateStore.snapshot,
  );
  const [fastExposure, selectFastExposure] = useUrlState("fastExposure", false);
  const [exposureError, setExposureError] = useState("");
  const track = enabled ? cameraRateStore.activeTrack() : null;
  const exposureSupported = !!track && supportsFastExposure(track);
  useEffect(() => {
    if (!track || !exposureSupported) return;
    let current = true;
    void setFastExposure(track, fastExposure).then(
      () => {
        if (current) setExposureError("");
      },
      (error: unknown) => {
        if (current)
          setExposureError(
            error instanceof Error ? error.message : String(error),
          );
      },
    );
    return () => {
      current = false;
      void setFastExposure(track, false).catch(() => {});
    };
  }, [track, fastExposure, exposureSupported]);
  const [preference, setPreference] = useUrlState(
    "fpsLimit",
    "",
    false,
    (value) => {
      // Preserve older links that deliberately selected a lower rendering limit.
      if (value !== "") return value;
      const legacy = Number(
        new URLSearchParams(location.search).get("targetFps"),
      );
      return legacy > 0 && legacy < 60 ? String(legacy) : value;
    },
  );
  const maximum = enabled
    ? Math.max(1, Math.round(rate.limit ?? rate.advertisedMaximum ?? 60))
    : 60;
  const appliedTarget = useRef(settings.targetFps);
  useEffect(() => {
    if (enabled && rate.limit === undefined) return;
    // Shared C keyboard settings can also change FPS outside the range input.
    if (settings.targetFps !== appliedTarget.current) {
      appliedTarget.current = settings.targetFps;
      setPreference(
        settings.targetFps >= maximum ? "auto" : String(settings.targetFps),
      );
      return;
    }
    const fps = effectiveCameraFps(preference, maximum);
    if (fps !== settings.targetFps) {
      appliedTarget.current = fps;
      if (!preference) setPreference("auto");
      setSettings({ ...settings, targetFps: fps });
    }
  }, [
    enabled,
    rate.limit,
    maximum,
    preference,
    settings,
    setSettings,
    setPreference,
  ]);
  const select = useCallback(
    (fps: number) => {
      appliedTarget.current = Math.min(fps, maximum);
      setPreference(fps >= maximum ? "auto" : String(fps));
      setSettings({ ...settings, targetFps: Math.min(fps, maximum) });
    },
    [maximum, settings, setSettings, setPreference],
  );
  return {
    fastExposure,
    selectFastExposure,
    exposureSupported,
    exposureError,
    target: settings.targetFps,
    rate: enabled ? rate : undefined,
    maximum,
    select,
    followsMaximum: !preference || preference === "auto",
  };
}
