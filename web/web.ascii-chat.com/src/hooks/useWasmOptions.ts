import { useCallback } from "react";
import {
  SettingsConfig,
  ColorMode,
  ColorFilter,
  Palette,
} from "../components/Settings";

export interface WasmOptionsManager {
  setDimensions: (width: number, height: number) => void;
  getDimensions: () => { width: number; height: number };
  setColorMode: (mode: ColorMode) => void;
  getColorMode: () => ColorMode;
  setColorFilter: (filter: ColorFilter) => void;
  getColorFilter: () => ColorFilter;
  setPalette: (palette: Palette) => void;
  getPalette: () => string;
  setPaletteChars: (chars: string) => void;
  getPaletteChars: () => string;
  setMatrixRain: (enabled: boolean) => void;
  getMatrixRain: () => boolean;
  setFlipX: (enabled: boolean) => void;
  getFlipX: () => boolean;
  setTargetFps: (fps: number) => void;
  getTargetFps: () => number;
  applySettings: (settings: SettingsConfig) => void;
}

/**
 * Create a WASM options manager that works with any WASM module
 * providing the standard getter/setter functions
 */
export function createWasmOptionsManager(
  setColorModeFn: (mode: number) => void,
  getColorModeFn: () => number,
  setColorFilterFn: (filter: number) => void,
  getColorFilterFn: () => number,
  setPaletteFn: (palette: Palette) => void,
  getPaletteFn: () => string,
  setPaletteCharsFn: (chars: string) => void,
  getPaletteCharsFn: () => string,
  setMatrixRainFn: (enabled: boolean) => void,
  getMatrixRainFn: () => boolean,
  setFlipXFn: (enabled: boolean) => void,
  getFlipXFn: () => boolean,
  setDimensionsFn: (width: number, height: number) => void,
  getDimensionsFn: () => { width: number; height: number },
  setTargetFpsFn: (fps: number) => void,
  getTargetFpsFn: () => number,
  mapColorMode: (mode: ColorMode) => number,
  mapColorFilter: (filter: ColorFilter) => number,
): WasmOptionsManager {
  return {
    setDimensions: setDimensionsFn,
    getDimensions: getDimensionsFn,
    setColorMode: (mode: ColorMode) => setColorModeFn(mapColorMode(mode)),
    getColorMode: () => {
      const mode = getColorModeFn();
      // Map back from WASM enum to ColorMode
      const modeMap: Record<number, ColorMode> = {
        0: "auto",
        1: "none",
        2: "16",
        3: "256",
        4: "truecolor",
      };
      return modeMap[mode] || "auto";
    },
    setColorFilter: (filter: ColorFilter) =>
      setColorFilterFn(mapColorFilter(filter)),
    getColorFilter: () => {
      const filter = getColorFilterFn();
      // Map back from WASM enum to ColorFilter
      const filterMap: Record<number, ColorFilter> = {
        0: "none",
        1: "black",
        2: "white",
        3: "green",
        4: "magenta",
        5: "fuchsia",
        6: "orange",
        7: "teal",
        8: "cyan",
        9: "pink",
        10: "red",
        11: "yellow",
        12: "rainbow",
      };
      return filterMap[filter] || "none";
    },
    setPalette: (palette: Palette) => setPaletteFn(palette),
    getPalette: getPaletteFn,
    setPaletteChars: setPaletteCharsFn,
    getPaletteChars: getPaletteCharsFn,
    setMatrixRain: setMatrixRainFn,
    getMatrixRain: getMatrixRainFn,
    setFlipX: setFlipXFn,
    getFlipX: getFlipXFn,
    setTargetFps: setTargetFpsFn,
    getTargetFps: getTargetFpsFn,
    applySettings: (settings: SettingsConfig) => {
      setDimensionsFn(settings.width, settings.height);
      setColorModeFn(mapColorMode(settings.colorMode));
      setColorFilterFn(mapColorFilter(settings.colorFilter));
      setPaletteFn(settings.palette);
      if (settings.palette === "custom" && settings.paletteChars) {
        setPaletteCharsFn(settings.paletteChars);
      }
      setMatrixRainFn(settings.matrixRain ?? false);
      setFlipXFn(settings.flipX ?? false);
      setTargetFpsFn(settings.targetFps);
    },
  };
}

/**
 * Hook to use WASM options manager
 */
export function useWasmOptions(manager: WasmOptionsManager | null) {
  const applySettings = useCallback(
    (settings: SettingsConfig) => {
      if (!manager) return;
      manager.applySettings(settings);
    },
    [manager],
  );

  return {
    manager,
    applySettings,
  };
}
