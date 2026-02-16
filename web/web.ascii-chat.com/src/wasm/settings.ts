/**
 * @file settings.ts
 * @brief Unified WASM settings API for mirror and client modes
 *
 * Provides a single, mode-agnostic API for all WASM option accessors.
 * Both mirror.ts and client.ts initialize this module with their own
 * options accessor, and React components use these functions directly.
 *
 * This eliminates code duplication between mirror and client implementations.
 */

import type { OptionAccessor } from "./common/optionsWrapper";

// Enums matching libasciichat definitions
export enum RenderMode {
  FOREGROUND = 0,
  BACKGROUND = 1,
  HALF_BLOCK = 2,
}

export enum ColorMode {
  AUTO = -1,
  NONE = 0,
  COLOR_16 = 1,
  COLOR_256 = 2,
  TRUECOLOR = 3,
}

export enum ColorFilter {
  NONE = 0,
  BLACK = 1,
  WHITE = 2,
  GREEN = 3,
  MAGENTA = 4,
  FUCHSIA = 5,
  ORANGE = 6,
  TEAL = 7,
  CYAN = 8,
  PINK = 9,
  RED = 10,
  YELLOW = 11,
  RAINBOW = 12,
}

export type Palette =
  | "standard"
  | "blocks"
  | "digital"
  | "minimal"
  | "cool"
  | "custom";

// Module-level options accessor (set by mirror.ts or client.ts during init)
let options: OptionAccessor | null = null;

/**
 * Initialize the settings module with an options accessor
 * Called by mirror.ts or client.ts after WASM module initialization
 * @internal
 */
export function initializeSettings(accessor: OptionAccessor): void {
  options = accessor;
}

/**
 * Cleanup settings module
 * @internal
 */
export function cleanupSettings(): void {
  options = null;
}

// ============================================================================
// Dimension Accessors
// ============================================================================

export function setWidth(width: number): void {
  if (!options) throw new Error("Settings not initialized");
  options.setInt("width", width);
}

export function getWidth(): number {
  if (!options) throw new Error("Settings not initialized");
  return options.getInt("width");
}

export function setHeight(height: number): void {
  if (!options) throw new Error("Settings not initialized");
  options.setInt("height", height);
}

export function getHeight(): number {
  if (!options) throw new Error("Settings not initialized");
  return options.getInt("height");
}

export function setDimensions(width: number, height: number): void {
  setWidth(width);
  setHeight(height);
}

export function getDimensions(): { width: number; height: number } {
  return {
    width: getWidth(),
    height: getHeight(),
  };
}

// ============================================================================
// Color Mode Accessors
// ============================================================================

export function setColorMode(mode: ColorMode): void {
  if (!options) throw new Error("Settings not initialized");
  options.setInt("color_mode", mode);
}

export function getColorMode(): ColorMode {
  if (!options) throw new Error("Settings not initialized");
  return options.getInt("color_mode");
}

// ============================================================================
// Color Filter Accessors
// ============================================================================

export function setColorFilter(filter: ColorFilter): void {
  if (!options) throw new Error("Settings not initialized");
  options.setInt("color_filter", filter);
}

export function getColorFilter(): ColorFilter {
  if (!options) throw new Error("Settings not initialized");
  return options.getInt("color_filter");
}

// ============================================================================
// Render Mode Accessors
// ============================================================================

export function setRenderMode(mode: RenderMode): void {
  if (!options) throw new Error("Settings not initialized");
  options.setInt("render_mode", mode);
}

export function getRenderMode(): RenderMode {
  if (!options) throw new Error("Settings not initialized");
  return options.getInt("render_mode");
}

// ============================================================================
// Palette Accessors
// ============================================================================

export function setPalette(palette: Palette): void {
  if (!options) throw new Error("Settings not initialized");
  options.setString("palette", palette);
}

export function getPalette(): Palette {
  if (!options) throw new Error("Settings not initialized");
  const paletteNum = options.getInt("palette");
  const paletteMap: Record<number, Palette> = {
    0: "standard",
    1: "blocks",
    2: "digital",
    3: "minimal",
    4: "cool",
    5: "custom",
  };
  return paletteMap[paletteNum] || "standard";
}

export function setPaletteChars(chars: string): void {
  if (!options) throw new Error("Settings not initialized");
  options.setString("palette_chars", chars);
}

export function getPaletteChars(): string {
  if (!options) throw new Error("Settings not initialized");
  return options.getString("palette_chars");
}

// ============================================================================
// Matrix Rain Accessors
// ============================================================================

export function setMatrixRain(enabled: boolean): void {
  if (!options) throw new Error("Settings not initialized");
  options.setBool("matrix_rain", enabled);
}

export function getMatrixRain(): boolean {
  if (!options) throw new Error("Settings not initialized");
  return options.getBool("matrix_rain");
}

// ============================================================================
// Horizontal Flip Accessors
// ============================================================================

export function setFlipX(enabled: boolean): void {
  if (!options) throw new Error("Settings not initialized");
  options.setBool("flip_x", enabled);
}

export function getFlipX(): boolean {
  if (!options) throw new Error("Settings not initialized");
  return options.getBool("flip_x");
}

// ============================================================================
// Target FPS Accessors
// ============================================================================

export function setTargetFps(fps: number): void {
  if (!options) throw new Error("Settings not initialized");
  options.setInt("target_fps", fps);
}

export function getTargetFps(): number {
  if (!options) throw new Error("Settings not initialized");
  return options.getInt("target_fps");
}
