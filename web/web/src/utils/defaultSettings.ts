import type { BinarySettingsConfig } from "../components";
import { defaultBrowserSettings } from "@ascii-chat/shared/wasm";

export const DEFAULT_PALETTE_CHARS = defaultBrowserSettings.paletteChars;

export const PALETTE_INPUT_DEBOUNCE_MS = 750;

export const DEFAULT_SETTINGS: BinarySettingsConfig = {
  width: 0,
  height: 0,
  targetFps: 60,
  colorMode: defaultBrowserSettings.colorMode,
  colorFilter: defaultBrowserSettings.colorFilter,
  palette: defaultBrowserSettings.palette,
  renderer: defaultBrowserSettings.renderer,
  stabilize: false,
  paletteExplicit: false,
  paletteChars: DEFAULT_PALETTE_CHARS,
  matrixRain: false,
  zalgo: 0,
  animationEnabled: false,
  animation: "matrix",
  faceFollow: false,
  faceFollowDebug: false,
  flipX: true,
  flipY: false,
  renderMode: defaultBrowserSettings.renderMode,
};

export function getDefaultSettings(): BinarySettingsConfig {
  return {
    ...DEFAULT_SETTINGS,
  };
}
