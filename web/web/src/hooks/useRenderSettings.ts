import {
  useCallback,
  useMemo,
  useRef,
  useLayoutEffect,
  type SetStateAction,
} from "react";
import type { BinarySettingsConfig } from "../components";
import { DEFAULT_SETTINGS } from "../utils/defaultSettings";
import { useUrlState } from "./useUrlState";

function defaultFlip(settings: BinarySettingsConfig): boolean {
  return !settings.animationEnabled || settings.animation === "matrix";
}

/** Source defaults apply until the user explicitly chooses an orientation. */
export function useRenderSettings() {
  const [preference, setPreference] = useUrlState(
    "horizontalFlip",
    "auto",
    false,
    (value) => {
      if (value !== "auto") return value;
      const params = new URLSearchParams(window.location.search);
      const animation = params.get("animation");
      const key =
        params.get("animationEnabled") === "true"
          ? animation === "waveform"
            ? "waveformFlipX"
            : animation === "fft"
              ? "fftFlipX"
              : "matrixFlipX"
          : "webcamFlipX";
      const legacy = params.get(key) ?? params.get("flipX");
      return legacy === null ? "auto" : legacy === "true" ? "on" : "off";
    },
  );
  // The applied value is derived below; do not serialize automatic defaults as
  // an explicit flipX choice when reloading a link.
  const [stored, setStored] = useUrlState(
    "render",
    DEFAULT_SETTINGS,
    false,
    (restored) => ({ ...restored, flipX: DEFAULT_SETTINGS.flipX ?? true }),
  );
  const settings = useMemo(
    () => ({
      ...stored,
      flipX: preference === "auto" ? defaultFlip(stored) : preference === "on",
    }),
    [stored, preference],
  );
  const current = useRef({ settings, preference });
  useLayoutEffect(() => {
    current.current = { settings, preference };
  }, [settings, preference]);
  const setSettings = useCallback(
    (action: SetStateAction<BinarySettingsConfig>) => {
      const { settings, preference } = current.current;
      const next = typeof action === "function" ? action(settings) : action;
      if (next === settings) return;
      const changedFlip = next.flipX !== settings.flipX;
      const selected = changedFlip ? (next.flipX ? "on" : "off") : preference;
      if (changedFlip) setPreference(selected);
      const flipX = selected === "auto" ? defaultFlip(next) : selected === "on";
      current.current = { settings: { ...next, flipX }, preference: selected };
      setStored({ ...next, flipX: DEFAULT_SETTINGS.flipX ?? true });
    },
    [setPreference, setStored],
  );
  return [settings, setSettings] as const;
}
