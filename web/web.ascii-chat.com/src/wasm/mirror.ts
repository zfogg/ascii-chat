// TypeScript wrapper for Mirror WASM module
// Provides type-safe interface to libasciichat mirror mode

// Type definition for the Emscripten module
interface MirrorModuleExports {
  _mirror_init(width: number, height: number): number;
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
  _mirror_convert_frame(rgba_data_ptr: number, src_width: number, src_height: number): number;
  _mirror_free_string(ptr: number): void;
  _malloc(size: number): number;
  _free(ptr: number): void;
}

interface MirrorModule {
  HEAPU8: Uint8Array;
  UTF8ToString(ptr: number): string;
  _mirror_init: MirrorModuleExports['_mirror_init'];
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
  AUTO = 0,
  NONE = 1,
  COLOR_16 = 2,
  COLOR_256 = 3,
  TRUECOLOR = 4
}

export enum ColorFilter {
  NONE = 0,
  BLACK = 1,
  WHITE = 2,
  GREEN = 3,
  MAGENTA = 4,
  CYAN = 5,
  YELLOW = 6,
  RED = 7,
  BLUE = 8,
  ORANGE = 9,
  PURPLE = 10,
  PINK = 11
}

// Import the Emscripten-generated module factory
// @ts-expect-error - Generated file without types
import MirrorModuleFactory from './dist/mirror.js';

let wasmModule: MirrorModule | null = null;
let frameCallCount = 0;

/**
 * Initialize the WASM module (call once at app start)
 */
export async function initMirrorWasm(width: number = 150, height: number = 60): Promise<void> {
  if (wasmModule) return;

  console.log('[WASM] Starting module initialization...');

  // Provide runtime environment functions for Emscripten
  wasmModule = await MirrorModuleFactory({
    // libsodium crypto random - returns 32-bit unsigned integer
    getRandomValue: function() {
      const buf = new Uint32Array(1);
      crypto.getRandomValues(buf);
      return buf[0];
    },
    // Enable Emscripten runtime error messages
    print: function(text: string) {
      console.log('[WASM stdout]', text);
    },
    printErr: function(text: string) {
      console.error('[WASM stderr]', text);
    },
    onAbort: function(what: any) {
      console.error('[WASM ABORT]', what);
    }
  });

  console.log('[WASM] Module loaded, calling _mirror_init...');
  console.log('[WASM] Module state:', {
    HEAPU8: wasmModule.HEAPU8 ? `${wasmModule.HEAPU8.length} bytes` : 'undefined',
    _mirror_init: typeof wasmModule._mirror_init,
    _malloc: typeof wasmModule._malloc,
    _free: typeof wasmModule._free,
    ccall: typeof (wasmModule as any).ccall
  });

  // Initialize libasciichat using ccall for proper type conversion
  console.log(`[WASM] Calling _mirror_init(${width}, ${height})`);
  const result = (wasmModule as any).ccall(
    'mirror_init',  // Function name without underscore
    'number',       // Return type
    ['number', 'number'],  // Argument types
    [width, height]  // Arguments
  );
  console.log('[WASM] _mirror_init returned:', result);

  if (result !== 0) {
    throw new Error('Failed to initialize mirror WASM module');
  }

  console.log('[WASM] Initialization complete');
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
      throw new Error('WASM mirror_convert_frame returned null');
    }

    // Convert C string to JavaScript string
    const asciiString = wasmModule.UTF8ToString(resultPtr);

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
 * Check if WASM module is ready
 */
export function isWasmReady(): boolean {
  return wasmModule !== null && wasmModule.HEAPU8 !== undefined;
}
