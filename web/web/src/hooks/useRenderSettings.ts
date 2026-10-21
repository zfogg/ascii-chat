import {
  useState,
  useCallback,
  useMemo,
  useRef,
  useLayoutEffect,
  type SetStateAction,
} from "react";
import type { BinarySettingsConfig } from "../components";
import { DEFAULT_SETTINGS } from "../utils/defaultSettings";
import { useUrlState } from "./useUrlState";

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
  const [appliedFlip, setAppliedFlip] = useState(stored.flipX ?? true);
  const settings = useMemo(
    () => ({
      ...stored,
      flipX: preference === "auto" ? appliedFlip : preference === "on",
      flipXExplicit: preference !== "auto",
    }),
    [stored, preference, appliedFlip],
  );
  const current = useRef({ settings, preference });
  useLayoutEffect(() => {
    current.current = { settings, preference };
  }, [settings, preference]);
  const commitSettings = useCallback(
    (next: BinarySettingsConfig, selected: string) => {
      const flipX = next.flipX ?? true;
      current.current = {
        settings: { ...next, flipX, flipXExplicit: selected !== "auto" },
        preference: selected,
      };
      setPreference(selected);
      setAppliedFlip(flipX);
      const { flipXExplicit: _explicit, ...storedNext } = next;
      setStored({ ...storedNext, flipX: DEFAULT_SETTINGS.flipX ?? true });
    },
    [setPreference, setStored],
  );
  // Only user edits select an orientation. C readbacks use a separate receiver.
  const setSettings = useCallback(
    (action: SetStateAction<BinarySettingsConfig>) => {
      const { settings, preference } = current.current;
      const next = typeof action === "function" ? action(settings) : action;
      if (next === settings) return;
      const selected =
        next.flipX !== settings.flipX
          ? next.flipX
            ? "on"
            : "off"
          : preference;
      commitSettings(next, selected);
    },
    [commitSettings],
  );
  const receiveSettings = useCallback(
    (next: BinarySettingsConfig) => {
      commitSettings(
        next,
        next.flipXExplicit ? (next.flipX ? "on" : "off") : "auto",
      );
    },
    [commitSettings],
  );
  return [settings, setSettings, receiveSettings] as const;
}
