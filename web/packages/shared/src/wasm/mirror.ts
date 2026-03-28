// Shared TypeScript wrapper for Mirror WASM module
// Provides type-safe interface to libasciichat mirror mode
// Decoupled from Emscripten factory location — callers provide the factory.

import { createOptionAccessor, type WasmModule } from "./common/optionsWrapper";
import {
  initializeOptions,
  cleanupOptions,
  RenderMode,
  ColorMode,
  ColorFilter,
  type Palette,
} from "./common/options";

// Type for WASM exports exposed to window.asciiChatWasm
interface AsciiChatWasmExports {
  _wasmModule?: MirrorModule;
  get_help_text?(mode: number, option_name: number): number;
}

// Type definition for the Emscripten module
interface MirrorModuleExports {
  _mirror_init_with_args(args_string: number): number;
  _mirror_cleanup(): void;
  _mirror_convert_frame(
    rgba_data_ptr: number,
    src_width: number,
    src_height: number,
  ): number;
  _mirror_free_string(ptr: number): void;
  _get_help_text(mode: number, option_name: number): number;
  // Terminal renderer functions (libvterm + FreeType)
  _term_renderer_create(cfg_ptr: number, out_ptr: number): number;
  _term_renderer_feed(r: number, ansi_data_ptr: number, len: number): number;
  _term_renderer_pixels(r: number): number;
  _term_renderer_width_px(r: number): number;
  _term_renderer_height_px(r: number): number;
  _term_renderer_pitch(r: number): number;
  _term_renderer_destroy(r: number): void;
  // Memory management
  _malloc(size: number): number;
  _free(ptr: number): void;
}

export interface MirrorModule extends WasmModule {
  canvas?: HTMLCanvasElement;
  _mirror_init_with_args?: MirrorModuleExports["_mirror_init_with_args"];
  _mirror_cleanup: MirrorModuleExports["_mirror_cleanup"];
  _mirror_convert_frame: MirrorModuleExports["_mirror_convert_frame"];
  _mirror_free_string: MirrorModuleExports["_mirror_free_string"];
  _get_help_text: MirrorModuleExports["_get_help_text"];
  _term_renderer_create: MirrorModuleExports["_term_renderer_create"];
  _term_renderer_feed: MirrorModuleExports["_term_renderer_feed"];
  _term_renderer_pixels: MirrorModuleExports["_term_renderer_pixels"];
  _term_renderer_width_px: MirrorModuleExports["_term_renderer_width_px"];
  _term_renderer_height_px: MirrorModuleExports["_term_renderer_height_px"];
  _term_renderer_pitch: MirrorModuleExports["_term_renderer_pitch"];
  _term_renderer_destroy: MirrorModuleExports["_term_renderer_destroy"];
}

// Re-export enums and types from shared options module
export { RenderMode, ColorMode, ColorFilter };
export type { Palette };

// Emscripten module factory type — callers provide this
export type EmscriptenModuleFactory = (
  moduleOverrides: Record<string, unknown>,
) => Promise<MirrorModule>;

let wasmModule: MirrorModule | null = null;

/**
 * Initialize the WASM module (call once at app start)
 * @param moduleFactory - Emscripten module factory function (from mirror.js)
 * @param options - Optional overrides (e.g., locateFile for cross-origin .wasm loading)
 */
export async function initMirrorWasm(
  moduleFactory: EmscriptenModuleFactory,
  options?: { locateFile?: (path: string) => string },
): Promise<void> {
  if (wasmModule) {
    return;
  }

  const moduleOverrides: Record<string, unknown> = {
    // libsodium crypto random - returns 32-bit unsigned integer
    getRandomValue: function () {
      const buf = new Uint32Array(1);
      crypto.getRandomValues(buf);
      return buf[0];
    },
  };

  if (options?.locateFile) {
    moduleOverrides["locateFile"] = options.locateFile;
  }

  try {
    wasmModule = await moduleFactory(moduleOverrides);
  } catch (err) {
    console.error("Failed to load WASM module:", err);
    throw err;
  }

  if (!wasmModule) {
    throw new Error("Failed to load WASM module");
  }

  try {
    // Initialize C options system with basic mirror mode arguments
    // This must be called before using any getter/setter functions
    if (wasmModule._mirror_init_with_args) {
      // Pass JSON array of arguments: ["mirror"]
      const argsJson = '["mirror"]';
      const strLen = wasmModule.lengthBytesUTF8(argsJson) + 1;
      const strPtr = wasmModule._malloc(strLen);
      if (!strPtr) {
        throw new Error("Failed to allocate memory for args");
      }

      try {
        wasmModule.stringToUTF8(argsJson, strPtr, strLen);
        const initResult = wasmModule._mirror_init_with_args(strPtr);
        if (initResult !== 0) {
          throw new Error(`Failed to initialize mirror C code: ${initResult}`);
        }
      } finally {
        wasmModule._free(strPtr);
      }
    }

    // Initialize shared options module with option accessor
    // This allows React components to call setColorMode, setPalette, etc.
    const optionsAccessor = createOptionAccessor(wasmModule);
    initializeOptions(optionsAccessor);

    // Expose WASM module to window for JavaScript access (e.g., tooltips)
    const globalWindow = globalThis as typeof globalThis & {
      asciiChatWasm: AsciiChatWasmExports;
    };
    globalWindow.asciiChatWasm = {
      _wasmModule: wasmModule,
    };
  } catch (err) {
    console.error("[WASM] Failed to initialize options:", err);
    throw err;
  }
}

/**
 * Cleanup WASM module resources
 */
export function cleanupMirrorWasm(): void {
  if (wasmModule) {
    wasmModule._mirror_cleanup();
    wasmModule = null;
    cleanupOptions();
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
  srcHeight: number,
): string {
  if (!wasmModule) {
    throw new Error(
      "WASM module not initialized. Call initMirrorWasm() first.",
    );
  }

  if (!wasmModule.HEAPU8) {
    throw new Error("WASM module not fully initialized. HEAPU8 is undefined.");
  }

  // Validate dimensions
  if (srcWidth <= 0 || srcHeight <= 0) {
    throw new Error(
      `Invalid image dimensions: ${srcWidth}x${srcHeight}. Width and height must be > 0.`,
    );
  }

  // Validate RGBA data size: must be exactly width * height * 4 bytes
  const expectedSize = srcWidth * srcHeight * 4;
  if (rgbaData.length !== expectedSize) {
    throw new Error(
      `RGBA data size mismatch: expected ${expectedSize} bytes (${srcWidth}x${srcHeight}*4), got ${rgbaData.length} bytes. This can cause memory access violations in WASM.`,
    );
  }

  // Allocate memory for RGBA data
  const dataPtr = wasmModule._malloc(rgbaData.length);

  // CRITICAL: In WASM, malloc returns 0 on failure (0 is a valid memory address but indicates error)
  // Accessing memory at address 0 would corrupt the heap
  if (dataPtr === 0 || dataPtr === null || typeof dataPtr === "undefined") {
    throw new Error(
      `Failed to allocate WASM memory for RGBA data (${rgbaData.length} bytes). WASM heap may be exhausted.`,
    );
  }

  if (dataPtr < 0) {
    throw new Error(`malloc returned invalid pointer: ${dataPtr}`);
  }

  try {
    // Validate RGBA data before copying to WASM
    if (!rgbaData || rgbaData.length === 0) {
      throw new Error(
        `Invalid RGBA data: buffer is ${!rgbaData ? "null/undefined" : "empty"}`,
      );
    }

    // Copy RGBA data to WASM memory
    wasmModule.HEAPU8.set(rgbaData, dataPtr);

    // Validate the C function pointers are available
    if (!wasmModule._mirror_convert_frame) {
      throw new Error("WASM function _mirror_convert_frame not found");
    }

    // Call WASM function (dimensions come from options set during init)
    const resultPtr = wasmModule._mirror_convert_frame(
      dataPtr,
      srcWidth,
      srcHeight,
    );

    if (!resultPtr) {
      throw new Error(
        `WASM mirror_convert_frame returned null after processing ${srcWidth}x${srcHeight}`,
      );
    }

    let asciiString: string;
    try {
      // Convert C string to JavaScript string
      asciiString = wasmModule.UTF8ToString(resultPtr);
    } finally {
      // Always free the result buffer (allocated by WASM), even if UTF8ToString fails
      wasmModule._mirror_free_string(resultPtr);
    }

    return asciiString;
  } finally {
    // Only free the input data buffer if malloc succeeded (dataPtr is not 0 or null)
    if (dataPtr && dataPtr > 0) {
      wasmModule._free(dataPtr);
    }
  }
}

/**
 * Check if WASM module is ready
 */
export function isWasmReady(): boolean {
  return wasmModule !== null && wasmModule.HEAPU8 !== undefined;
}

/**
 * Get the WASM module instance
 */
export function getMirrorModule(): MirrorModule | null {
  return wasmModule;
}
