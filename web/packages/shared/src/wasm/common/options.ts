/**
 * @file options.ts
 * @brief Unified WASM options API for mirror and client modes
 *
 * Provides a single, mode-agnostic API for all WASM option accessors.
 * Both mirror.ts and client.ts initialize this module with their own
 * options accessor, and React components use these functions directly.
 *
 * This eliminates code duplication between mirror and client implementations.
 */

import type { OptionAccessor } from "./optionsWrapper";

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

// TODO: Query C code's options struct directly instead of caching in JavaScript.
// Currently caching palette, flipX, flipY in JavaScript because WASM getters
// (_get_palette, _get_flip_x, _get_flip_y) return unreliable values during rendering.
// The real fix: the C code has an internal options struct with the actual state.
// Instead of JavaScript caching, we should expose a getter that queries that struct
// directly, making the C code the single source of truth. This would eliminate
// the need for JavaScript-side caching and ensure mirror_init_with_args works
// correctly with the real state values.
//
// Known bugs (WASM only, don't appear in terminal):
// 1. --palette passed to mirror_init_with_args returns different values each time
// 2. --flip-x didn't work correctly (needs investigation, may require querying C struct first)
// 3. --matrix doesn't render green on first frame after resize (renders truecolor instead)
// These issues don't occur in the terminal version, suggesting WASM-specific state handling.

// Cache for values since WASM getters may not track set values reliably
let cachedPalette: Palette = "standard";
let cachedPaletteChars: string = "";
let cachedFlipX: boolean = false;
let cachedFlipY: boolean = false;

/**
 * Initialize the options module with an options accessor
 * Called by mirror.ts or client.ts after WASM module initialization
 * @internal
 */
export function initializeOptions(accessor: OptionAccessor): void {
  options = accessor;
  cachedPalette = "standard";
  cachedPaletteChars = "";
  cachedFlipX = false;
  cachedFlipY = false;
}

/**
 * Cleanup options module
 * @internal
 */
export function cleanupOptions(): void {
  options = null;
  cachedPalette = "standard";
  cachedPaletteChars = "";
  cachedFlipX = false;
  cachedFlipY = false;
}

/**
 * Check if the options module has been initialized
 * @returns true if options are ready, false otherwise
 */
export function isOptionsInitialized(): boolean {
  return options !== null;
}

// ============================================================================
// Dimension Accessors
// ============================================================================

export function setWidth(width: number): void {
  if (!options) throw new Error("Options not initialized");
  options.setInt("width", width);
}

export function getWidth(): number {
  if (!options) throw new Error("Options not initialized");
  return options.getInt("width");
}

export function setHeight(height: number): void {
  if (!options) throw new Error("Options not initialized");
  options.setInt("height", height);
}

export function getHeight(): number {
  if (!options) throw new Error("Options not initialized");
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
  if (!options) throw new Error("Options not initialized");
  options.setInt("color_mode", mode);
}

export function getColorMode(): ColorMode {
  if (!options) throw new Error("Options not initialized");
  return options.getInt("color_mode");
}

// ============================================================================
// Color Filter Accessors
// ============================================================================

export function setColorFilter(filter: ColorFilter): void {
  if (!options) throw new Error("Options not initialized");
  options.setInt("color_filter", filter);
}

export function getColorFilter(): ColorFilter {
  if (!options) throw new Error("Options not initialized");
  return options.getInt("color_filter");
}

// ============================================================================
// Render Mode Accessors
// ============================================================================

export function setRenderMode(mode: RenderMode): void {
  if (!options) throw new Error("Options not initialized");
  options.setInt("render_mode", mode);
}

export function getRenderMode(): RenderMode {
  if (!options) throw new Error("Options not initialized");
  return options.getInt("render_mode");
}

// ============================================================================
// Palette Accessors
// ============================================================================

export function setPalette(palette: Palette): void {
  if (!options) throw new Error("Options not initialized");
  options.setString("palette", palette);
  cachedPalette = palette;
}

export function getPalette(): Palette {
  if (!options) throw new Error("Options not initialized");
  return cachedPalette;
}

export function setPaletteChars(chars: string): void {
  if (!options) throw new Error("Options not initialized");
  options.setString("palette_chars", chars);
  cachedPaletteChars = chars;
}

export function getPaletteChars(): string {
  if (!options) throw new Error("Options not initialized");
  return cachedPaletteChars;
}

// ============================================================================
// Matrix Rain Accessors
// ============================================================================

export function setMatrixRain(enabled: boolean): void {
  if (!options) throw new Error("Options not initialized");
  options.setBool("matrix_rain", enabled);
}

export function getMatrixRain(): boolean {
  if (!options) throw new Error("Options not initialized");
  return options.getBool("matrix_rain");
}

// ============================================================================
// Horizontal Flip Accessors
// ============================================================================

export function setFlipX(enabled: boolean): void {
  if (!options) throw new Error("Options not initialized");
  options.setBool("flip_x", enabled);
  cachedFlipX = enabled;
}

export function getFlipX(): boolean {
  if (!options) throw new Error("Options not initialized");
  return cachedFlipX;
}

// ============================================================================
// Vertical Flip Accessors
// ============================================================================

export function setFlipY(enabled: boolean): void {
  if (!options) throw new Error("Options not initialized");
  options.setBool("flip_y", enabled);
  cachedFlipY = enabled;
}

export function getFlipY(): boolean {
  if (!options) throw new Error("Options not initialized");
  return cachedFlipY;
}

// ============================================================================
// Target FPS Accessors
// ============================================================================

export function setTargetFps(fps: number): void {
  if (!options) throw new Error("Options not initialized");
  options.setInt("target_fps", fps);
}

export function getTargetFps(): number {
  if (!options) throw new Error("Options not initialized");
  return options.getInt("target_fps");
}
