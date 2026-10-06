import type { BinarySettingsConfig } from "../components";

export const DEFAULT_SETTINGS: BinarySettingsConfig = {
  width: 640,
  height: 480,
  targetFps: 60,
  colorMode: "truecolor",
  colorFilter: "none",
  palette: "standard",
  paletteChars: " =#░░▒▒▓▓██",
  matrixRain: false,
  flipX: true,
};

export function getDefaultSettings(): BinarySettingsConfig {
  return {
    ...DEFAULT_SETTINGS,
  };
}
