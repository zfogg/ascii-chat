// TypeScript wrapper for Mirror WASM module
// Provides type-safe interface to libasciichat mirror mode

// Type definition for the Emscripten module
interface MirrorModuleExports {
  _mirror_init_with_args(args_string: number): number;
  _mirror_cleanup(): void;
  _mirror_set_width(width: number): number;
  _mirror_set_height(height: number): number;
  _mirror_get_width(): number;
  _mirror_get_height(): number;
  _mirror_set_render_mode(mode: number): number;
  _mirror_get_render_mode(): number;
  _mirror_set_color_mode(mode: number): number;
  _mirror_get_color_mode(): number;
  _mirror_set_color_filter(filter: number): number;
  _mirror_get_color_filter(): number;
  _mirror_set_palette(palette_string_ptr: number): number;
  _mirror_get_palette(): number;
  _mirror_convert_frame(rgba_data_ptr: number, src_width: number, src_height: number): number;
  _mirror_free_string(ptr: number): void;
  _malloc(size: number): number;
  _free(ptr: number): void;
}

interface MirrorModule {
  HEAPU8: Uint8Array;
  UTF8ToString(ptr: number): string;
  stringToUTF8(str: string, outPtr: number, maxBytesToWrite: number): void;
  lengthBytesUTF8(str: string): number;
  _mirror_init_with_args: MirrorModuleExports['_mirror_init_with_args'];
  _mirror_cleanup: MirrorModuleExports['_mirror_cleanup'];
  _mirror_set_width: MirrorModuleExports['_mirror_set_width'];
  _mirror_set_height: MirrorModuleExports['_mirror_set_height'];
  _mirror_get_width: MirrorModuleExports['_mirror_get_width'];
  _mirror_get_height: MirrorModuleExports['_mirror_get_height'];
  _mirror_set_render_mode: MirrorModuleExports['_mirror_set_render_mode'];
  _mirror_get_render_mode: MirrorModuleExports['_mirror_get_render_mode'];
  _mirror_set_color_mode: MirrorModuleExports['_mirror_set_color_mode'];
  _mirror_get_color_mode: MirrorModuleExports['_mirror_get_color_mode'];
  _mirror_set_color_filter: MirrorModuleExports['_mirror_set_color_filter'];
  _mirror_get_color_filter: MirrorModuleExports['_mirror_get_color_filter'];
  _mirror_set_palette: MirrorModuleExports['_mirror_set_palette'];
  _mirror_get_palette: MirrorModuleExports['_mirror_get_palette'];
  _mirror_convert_frame: MirrorModuleExports['_mirror_convert_frame'];
  _mirror_free_string: MirrorModuleExports['_mirror_free_string'];
  _malloc: MirrorModuleExports['_malloc'];
  _free: MirrorModuleExports['_free'];
}

// Enums matching libasciichat definitions
export enum RenderMode {
  FOREGROUND = 0,
  BACKGROUND = 1,
  HALF_BLOCK = 2
}

export enum ColorMode {
  AUTO = -1,
  NONE = 0,
  COLOR_16 = 1,
  COLOR_256 = 2,
  TRUECOLOR = 3
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
  YELLOW = 11
}

// Import the Emscripten-generated module factory
// @ts-expect-error - Generated file without types
import MirrorModuleFactory from './dist/mirror.js';

let wasmModule: MirrorModule | null = null;
let frameCallCount = 0;

export type Palette = 'standard' | 'blocks' | 'digital' | 'minimal' | 'cool' | 'custom';

export interface MirrorInitOptions {
  width?: number;
  height?: number;
  colorMode?: ColorMode;
  colorFilter?: ColorFilter;
  renderMode?: RenderMode;
  palette?: Palette;
}

// Map enum values to CLI option strings
const colorModeNames: Record<ColorMode, string> = {
  [ColorMode.AUTO]: 'auto',
  [ColorMode.NONE]: 'none',
  [ColorMode.COLOR_16]: '16',
  [ColorMode.COLOR_256]: '256',
  [ColorMode.TRUECOLOR]: 'truecolor'
};

const colorFilterNames: Record<ColorFilter, string> = {
  [ColorFilter.NONE]: 'none',
  [ColorFilter.BLACK]: 'black',
  [ColorFilter.WHITE]: 'white',
  [ColorFilter.GREEN]: 'green',
  [ColorFilter.MAGENTA]: 'magenta',
  [ColorFilter.FUCHSIA]: 'fuchsia',
  [ColorFilter.ORANGE]: 'orange',
  [ColorFilter.TEAL]: 'teal',
  [ColorFilter.CYAN]: 'cyan',
  [ColorFilter.PINK]: 'pink',
  [ColorFilter.RED]: 'red',
  [ColorFilter.YELLOW]: 'yellow'
};

const renderModeNames: Record<RenderMode, string> = {
  [RenderMode.FOREGROUND]: 'foreground',
  [RenderMode.BACKGROUND]: 'background',
  [RenderMode.HALF_BLOCK]: 'half-block'
};

/**
 * Initialize the WASM module (call once at app start)
 */
export async function initMirrorWasm(options: MirrorInitOptions = {}): Promise<void> {
  if (wasmModule) return;

  // Provide runtime environment functions for Emscripten
  wasmModule = await MirrorModuleFactory({
    // libsodium crypto random - returns 32-bit unsigned integer
    getRandomValue: function() {
      const buf = new Uint32Array(1);
      crypto.getRandomValues(buf);
      return buf[0];
    }
  });

  if (!wasmModule) {
    throw new Error('Failed to load WASM module');
  }

  // Build argument string for options_init()
  const args: string[] = ['mirror'];

  if (options.width !== undefined) {
    args.push('--width', options.width.toString());
  }
  if (options.height !== undefined) {
    args.push('--height', options.height.toString());
  }
  if (options.colorMode !== undefined) {
    args.push('--color-mode', colorModeNames[options.colorMode]);
  }
  if (options.colorFilter !== undefined) {
    args.push('--color-filter', colorFilterNames[options.colorFilter]);
  }
  if (options.renderMode !== undefined) {
    args.push('--render-mode', renderModeNames[options.renderMode]);
  }
  if (options.palette !== undefined) {
    args.push('--palette', options.palette);
  }

  const argsString = args.join(' ');
  console.log('[WASM] Initializing with args:', argsString);

  // Allocate string in WASM memory
  const strLen = wasmModule.lengthBytesUTF8(argsString) + 1;
  const strPtr = wasmModule._malloc(strLen);
  if (!strPtr) {
    throw new Error('Failed to allocate memory for args string');
  }

  try {
    wasmModule.stringToUTF8(argsString, strPtr, strLen);

    // Initialize libasciichat with parsed arguments
    const result = wasmModule._mirror_init_with_args(strPtr);
    if (result !== 0) {
      throw new Error('Failed to initialize mirror WASM module');
    }
  } finally {
    wasmModule._free(strPtr);
  }
}

/**
 * Cleanup WASM module resources
 */
export function cleanupMirrorWasm(): void {
  if (wasmModule) {
    wasmModule._mirror_cleanup();
    wasmModule = null;
  }
}

/**
 * Convert RGBA image data to ASCII string
 *
 * @param rgbaData - RGBA pixel data from canvas (4 bytes per pixel)
 * @param srcWidth - Source image width
 * @param srcHeight - Source image height
 * @returns ASCII art string
 */
export function convertFrameToAscii(
  rgbaData: Uint8Array,
  srcWidth: number,
  srcHeight: number
): string {
  if (!wasmModule) {
    throw new Error('WASM module not initialized. Call initMirrorWasm() first.');
  }

  if (!wasmModule.HEAPU8) {
    throw new Error('WASM module not fully initialized. HEAPU8 is undefined.');
  }

  frameCallCount++;
  if (frameCallCount % 300 === 0) {
    console.log(`[WASM] Processed ${frameCallCount} frames`);
  }

  // Allocate WASM memory for input RGBA data
  const dataPtr = wasmModule._malloc(rgbaData.length);
  if (!dataPtr) {
    throw new Error(`Failed to allocate WASM memory for RGBA data (${rgbaData.length} bytes)`);
  }

  try {
    // Copy RGBA data to WASM memory
    wasmModule.HEAPU8.set(rgbaData, dataPtr);

    // Call WASM function (dimensions come from options set during init)
    const resultPtr = wasmModule._mirror_convert_frame(
      dataPtr,
      srcWidth,
      srcHeight
    );

    if (!resultPtr) {
      console.error('[WASM] mirror_convert_frame returned NULL pointer');
      throw new Error('WASM mirror_convert_frame returned null');
    }

    // Convert C string to JavaScript string
    const asciiString = wasmModule.UTF8ToString(resultPtr);

    // Debug: log string length on palette change
    if (frameCallCount % 30 === 0) {
      console.log(`[WASM] Frame result: ptr=${resultPtr}, length=${asciiString.length}, first 50 chars:`, asciiString.substring(0, 50));
    }

    // Free the result buffer (allocated by WASM)
    wasmModule._mirror_free_string(resultPtr);

    return asciiString;
  } catch (err) {
    console.error('[WASM] convertFrameToAscii error:', err);
    throw err;
  } finally {
    // Always free the input buffer
    wasmModule._free(dataPtr);
  }
}

/**
 * Set ASCII output dimensions
 */
export function setDimensions(width: number, height: number): void {
  if (!wasmModule) throw new Error('WASM module not initialized');

  if (wasmModule._mirror_set_width(width) !== 0) {
    throw new Error(`Invalid width: ${width}`);
  }
  if (wasmModule._mirror_set_height(height) !== 0) {
    throw new Error(`Invalid height: ${height}`);
  }
}

/**
 * Get current ASCII dimensions
 */
export function getDimensions(): { width: number; height: number } {
  if (!wasmModule) throw new Error('WASM module not initialized');

  return {
    width: wasmModule._mirror_get_width(),
    height: wasmModule._mirror_get_height()
  };
}

/**
 * Set render mode (foreground, background, or half-block)
 */
export function setRenderMode(mode: RenderMode): void {
  if (!wasmModule) throw new Error('WASM module not initialized');

  if (wasmModule._mirror_set_render_mode(mode) !== 0) {
    throw new Error(`Invalid render mode: ${mode}`);
  }
}

/**
 * Get current render mode
 */
export function getRenderMode(): RenderMode {
  if (!wasmModule) throw new Error('WASM module not initialized');
  return wasmModule._mirror_get_render_mode();
}

/**
 * Set color mode
 */
export function setColorMode(mode: ColorMode): void {
  if (!wasmModule) throw new Error('WASM module not initialized');

  if (wasmModule._mirror_set_color_mode(mode) !== 0) {
    throw new Error(`Invalid color mode: ${mode}`);
  }
}

/**
 * Get current color mode
 */
export function getColorMode(): ColorMode {
  if (!wasmModule) throw new Error('WASM module not initialized');
  return wasmModule._mirror_get_color_mode();
}

/**
 * Set color filter
 */
export function setColorFilter(filter: ColorFilter): void {
  if (!wasmModule) throw new Error('WASM module not initialized');

  if (wasmModule._mirror_set_color_filter(filter) !== 0) {
    throw new Error(`Invalid color filter: ${filter}`);
  }
}

/**
 * Get current color filter
 */
export function getColorFilter(): ColorFilter {
  if (!wasmModule) throw new Error('WASM module not initialized');
  return wasmModule._mirror_get_color_filter();
}

/**
 * Set palette
 */
export function setPalette(palette: Palette): void {
  if (!wasmModule) throw new Error('WASM module not initialized');

  // Allocate string in WASM memory
  const strLen = wasmModule.lengthBytesUTF8(palette) + 1;
  const strPtr = wasmModule._malloc(strLen);
  if (!strPtr) {
    throw new Error('Failed to allocate memory for palette string');
  }

  try {
    wasmModule.stringToUTF8(palette, strPtr, strLen);
    const result = wasmModule._mirror_set_palette(strPtr);
    if (result !== 0) {
      throw new Error(`Failed to set palette: ${palette}`);
    }
  } finally {
    wasmModule._free(strPtr);
  }
}

/**
 * Get current palette
 */
export function getPalette(): number {
  if (!wasmModule) throw new Error('WASM module not initialized');
  return wasmModule._mirror_get_palette();
}

/**
 * Check if WASM module is ready
 */
export function isWasmReady(): boolean {
  return wasmModule !== null && wasmModule.HEAPU8 !== undefined;
}
