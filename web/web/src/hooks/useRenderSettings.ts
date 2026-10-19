import {
  useCallback,
  useMemo,
  useState,
  useRef,
  useLayoutEffect,
  type SetStateAction,
} from "react";
import type { BinarySettingsConfig } from "../components";
import { DEFAULT_SETTINGS } from "../utils/defaultSettings";
import { readUrlValue } from "../utils/urlState";
import { useUrlState } from "./useUrlState";

const defaults = {
  webcamFlipX: true,
  matrixFlipX: true,
  waveformFlipX: false,
  fftFlipX: false,
};
type FlipKey = keyof typeof defaults;
function flipKey(settings: BinarySettingsConfig): FlipKey {
  if (!settings.animationEnabled) return "webcamFlipX";
  return settings.animation === "waveform"
    ? "waveformFlipX"
    : settings.animation === "fft"
      ? "fftFlipX"
      : "matrixFlipX";
}

/** Keep source/animation preferences separate from the currently applied C option. */
export function useRenderSettings() {
  const [legacy] = useState(() => {
    const params = new URLSearchParams(window.location.search);
    const initial = readUrlValue("render", DEFAULT_SETTINGS);
    const key = flipKey(initial);
    return params.has("flipX") && !params.has(key)
      ? { [key]: initial.flipX ?? defaults[key] }
      : null;
  });
  const [stored, setStored] = useUrlState("render", DEFAULT_SETTINGS);
  const [preferences, setPreferences] = useUrlState(
    "horizontalFlip",
    defaults,
    false,
    (restored) => ({ ...restored, ...legacy }),
  );
  const flips = preferences;
  const settings = useMemo(
    () => ({ ...stored, flipX: flips[flipKey(stored)] }),
    [stored, flips],
  );
  const current = useRef({ settings, flips });
  useLayoutEffect(() => {
    current.current = { settings, flips };
  }, [settings, flips]);
  const setSettings = useCallback(
    (action: SetStateAction<BinarySettingsConfig>) => {
      const { settings, flips } = current.current;
      const next = typeof action === "function" ? action(settings) : action;
      if (next === settings) return;
      const key = flipKey(next);
      const changedFlip =
        key === flipKey(settings) && next.flipX !== settings.flipX;
      const flipX = changedFlip ? !!next.flipX : flips[key];
      if (changedFlip)
        setPreferences((previous) => ({ ...previous, [key]: flipX }));
      const updated = { ...next, flipX };
      current.current = {
        settings: updated,
        flips: changedFlip ? { ...flips, [key]: flipX } : flips,
      };
      setStored(updated);
    },
    [setPreferences, setStored],
  );
  return [settings, setSettings] as const;
}
