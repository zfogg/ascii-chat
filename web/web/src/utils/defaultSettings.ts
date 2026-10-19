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
  zalgo: 0,
  animationEnabled: false,
  animation: "matrix",
  faceFollow: false,
  faceFollowDebug: false,
  flipX: true,
  flipY: false,
  renderMode: 0,
};

export function getDefaultSettings(): BinarySettingsConfig {
  return {
    ...DEFAULT_SETTINGS,
  };
}
