import type { SettingsConfig } from "../components";

export const DEFAULT_SETTINGS: SettingsConfig = {
  width: 640,
  height: 480,
  targetFps: 60,
  colorMode: "none",
  colorFilter: "none",
  palette: "standard",
  paletteChars: " =#░░▒▒▓▓██",
  matrixRain: false,
  flipX: false,
};

/**
 * Get default settings with platform-specific overrides
 * @param isMacOS Whether the current platform is macOS
 */
export function getDefaultSettings(isMacOS: boolean = false): SettingsConfig {
  return {
    ...DEFAULT_SETTINGS,
    flipX: isMacOS,
  };
}
