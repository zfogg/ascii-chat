import type { BinarySettingsConfig } from "../components";

export const DEFAULT_SETTINGS: BinarySettingsConfig = {
  width: 0,
  height: 0,
  targetFps: 60,
  colorMode: "truecolor",
  colorFilter: "none",
  palette: "standard",
  paletteChars: " =#░░▒▒▓▓██",
  matrixRain: false,
  animationEnabled: false,
  animation: "matrix",
  flipX: true,
};

export function getDefaultSettings(): BinarySettingsConfig {
  return {
    ...DEFAULT_SETTINGS,
  };
}
