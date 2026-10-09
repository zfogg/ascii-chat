import { useEffect, type Dispatch, type SetStateAction } from "react";
import type { BinarySettingsConfig } from "../components";

/** Start each page load with automatic sizing; preserve overrides on resize. */
export function useAutoGridSize(
  setSettings: Dispatch<SetStateAction<BinarySettingsConfig>>,
) {
  useEffect(() => {
    setSettings((settings) =>
      settings.width === 0 && settings.height === 0
        ? settings
        : { ...settings, width: 0, height: 0 },
    );
  }, [setSettings]);
}
