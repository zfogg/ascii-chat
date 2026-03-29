import type { SettingsConfig } from "../components";

export const DEFAULT_SETTINGS: SettingsConfig = {
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

export function getDefaultSettings(): SettingsConfig {
  return {
    ...DEFAULT_SETTINGS,
  };
}
