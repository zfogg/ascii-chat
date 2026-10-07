import { useCallback } from "react";
import {
  getColorFilter,
  getColorMode,
  getDimensions,
  getFft,
  getFlipX,
  getMatrixRain,
  getWaveform,
  getPalette,
  getPaletteChars,
  getTargetFps,
  isOptionsInitialized,
  isWasmReady,
  setColorFilter,
  setColorMode,
  setDimensions,
  setFft,
  setFlipX,
  setMatrixRain,
  setPalette,
  setPaletteChars,
  setTargetFps,
  setWaveform,
} from "@ascii-chat/shared/wasm";
import type {
  BinarySettingsConfig,
  ColorMode,
  ColorFilter,
  Palette,
} from "../components";
import { mapColorFilterToWasm, mapColorModeToWasm } from "../utils";

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
  setWaveform: (enabled: boolean) => void;
  getWaveform: () => boolean;
  setFft: (enabled: boolean) => void;
  getFft: () => boolean;
  setFlipX: (enabled: boolean) => void;
  getFlipX: () => boolean;
  setTargetFps: (fps: number) => void;
  getTargetFps: () => number;
  applySettings: (settings: BinarySettingsConfig) => void;
}

/**
 * The browser mirror renderer owns the shared options module used by all
 * browser video modes. Keep its option wiring in one place so Client and
 * Discovery apply exactly the same settings as Mirror.
 */
export function createMirrorWasmOptionsManager(): WasmOptionsManager {
  return createWasmOptionsManager(
    setColorMode,
    getColorMode,
    setColorFilter,
    getColorFilter,
    setPalette,
    getPalette,
    setPaletteChars,
    getPaletteChars,
    setMatrixRain,
    getMatrixRain,
    setWaveform,
    getWaveform,
    setFft,
    getFft,
    setFlipX,
    getFlipX,
    setDimensions,
    getDimensions,
    setTargetFps,
    getTargetFps,
    mapColorModeToWasm,
    mapColorFilterToWasm,
  );
}

/** Apply settings when the shared Mirror WASM option accessor is available. */
export function applyMirrorWasmSettings(
  settings: BinarySettingsConfig,
): boolean {
  if (!isWasmReady() || !isOptionsInitialized()) return false;
  createMirrorWasmOptionsManager().applySettings(settings);
  return true;
}

/** Keep the shared renderer dimensions synchronized with the current canvas. */
export function setMirrorWasmDimensions(cols: number, rows: number): boolean {
  if (!isWasmReady() || !isOptionsInitialized()) return false;
  createMirrorWasmOptionsManager().setDimensions(cols, rows);
  return true;
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
  setWaveformFn: (enabled: boolean) => void,
  getWaveformFn: () => boolean,
  setFftFn: (enabled: boolean) => void,
  getFftFn: () => boolean,
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
      // Values match terminal_color_mode_t: AUTO=-1, NONE=0, 16=1, 256=2, TRUECOLOR=3
      const modeMap: Record<number, ColorMode> = {
        [-1]: "auto",
        0: "none",
        1: "16",
        2: "256",
        3: "truecolor",
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
    setWaveform: setWaveformFn,
    getWaveform: getWaveformFn,
    setFft: setFftFn,
    getFft: getFftFn,
    setFlipX: setFlipXFn,
    getFlipX: getFlipXFn,
    setTargetFps: setTargetFpsFn,
    getTargetFps: getTargetFpsFn,
    applySettings: (settings: BinarySettingsConfig) => {
      console.log("[WasmOptionsManager] applySettings called with:", settings);
      try {
        console.log(
          "[WasmOptionsManager] Setting colorMode:",
          settings.colorMode,
          "->",
          mapColorMode(settings.colorMode),
        );
        setColorModeFn(mapColorMode(settings.colorMode));
        console.log(
          "[WasmOptionsManager] Setting colorFilter:",
          settings.colorFilter,
          "->",
          mapColorFilter(settings.colorFilter),
        );
        setColorFilterFn(mapColorFilter(settings.colorFilter));
        console.log("[WasmOptionsManager] Setting palette:", settings.palette);
        setPaletteFn(settings.palette);
        if (settings.palette === "custom" && settings.paletteChars) {
          console.log(
            "[WasmOptionsManager] Setting paletteChars:",
            settings.paletteChars,
          );
          setPaletteCharsFn(settings.paletteChars);
        }
        console.log(
          "[WasmOptionsManager] Setting matrixRain:",
          settings.matrixRain ?? false,
        );
        const animation = settings.animation ?? "matrix";
        const enabled = settings.animationEnabled ?? settings.matrixRain ?? false;
        setMatrixRainFn(enabled && animation === "matrix");
        setWaveformFn(enabled && animation === "waveform");
        setFftFn(enabled && animation === "fft");
        console.log(
          "[WasmOptionsManager] Setting flipX:",
          settings.flipX ?? false,
        );
        setFlipXFn(settings.flipX ?? false);
        console.log(
          "[WasmOptionsManager] Setting targetFps:",
          settings.targetFps,
        );
        setTargetFpsFn(settings.targetFps);
        console.log("[WasmOptionsManager] All settings applied successfully");
      } catch (err) {
        console.error("[WasmOptionsManager] Error applying settings:", err);
        throw err;
      }
    },
  };
}

/**
 * Hook to use WASM options manager
 */
export function useWasmOptions(manager: WasmOptionsManager | null) {
  const applySettings = useCallback(
    (settings: BinarySettingsConfig) => {
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
