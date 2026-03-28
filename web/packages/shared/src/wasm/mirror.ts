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
  // ASCII renderer functions (libvterm + FreeType + raylib)
  _ascii_renderer_init(pixel_width: number, pixel_height: number): void;
  _ascii_renderer_render_frame(ansi_data_ptr: number, len: number): void;
  _ascii_renderer_resize(pixel_width: number, pixel_height: number): void;
  _ascii_renderer_get_cols(): number;
  _ascii_renderer_get_rows(): number;
  _ascii_renderer_get_framebuffer(): number;
  _ascii_renderer_get_framebuffer_width(): number;
  _ascii_renderer_get_framebuffer_height(): number;
  _ascii_renderer_get_framebuffer_stride(): number;
  _ascii_renderer_shutdown(): void;
  // Memory management
  _malloc(size: number): number;
  _free(ptr: number): void;
}

interface MirrorModule extends WasmModule {
  _mirror_init_with_args?: MirrorModuleExports["_mirror_init_with_args"];
  _mirror_cleanup: MirrorModuleExports["_mirror_cleanup"];
  _mirror_convert_frame: MirrorModuleExports["_mirror_convert_frame"];
  _mirror_free_string: MirrorModuleExports["_mirror_free_string"];
  _get_help_text: MirrorModuleExports["_get_help_text"];
  _ascii_renderer_init: MirrorModuleExports["_ascii_renderer_init"];
  _ascii_renderer_render_frame: MirrorModuleExports["_ascii_renderer_render_frame"];
  _ascii_renderer_resize: MirrorModuleExports["_ascii_renderer_resize"];
  _ascii_renderer_get_cols: MirrorModuleExports["_ascii_renderer_get_cols"];
  _ascii_renderer_get_rows: MirrorModuleExports["_ascii_renderer_get_rows"];
  _ascii_renderer_get_framebuffer: MirrorModuleExports["_ascii_renderer_get_framebuffer"];
  _ascii_renderer_get_framebuffer_width: MirrorModuleExports["_ascii_renderer_get_framebuffer_width"];
  _ascii_renderer_get_framebuffer_height: MirrorModuleExports["_ascii_renderer_get_framebuffer_height"];
  _ascii_renderer_get_framebuffer_stride: MirrorModuleExports["_ascii_renderer_get_framebuffer_stride"];
  _ascii_renderer_shutdown: MirrorModuleExports["_ascii_renderer_shutdown"];
}

// Re-export enums and types from shared options module
export { RenderMode, ColorMode, ColorFilter };
export type { Palette };

// Emscripten module factory type — callers provide this
export type EmscriptenModuleFactory = (
  moduleOverrides: Record<string, unknown>,
) => Promise<MirrorModule>;

let wasmModule: MirrorModule | null = null;
let frameCallCount = 0;

/**
 * Initialize the WASM module (call once at app start)
 * @param moduleFactory - Emscripten module factory function (from mirror.js)
 * @param options - Optional overrides (e.g., locateFile for cross-origin .wasm loading)
 */
export async function initMirrorWasm(
  moduleFactory: EmscriptenModuleFactory,
  options?: { locateFile?: (path: string) => string },
): Promise<void> {
  console.log(
    `[WASM Mirror] initMirrorWasm called at ${new Date().toISOString()}`
  );

  if (wasmModule) {
    console.log("[WASM Mirror] WASM module already initialized, returning");
    return;
  }

  console.log("[WASM Mirror] Starting module factory...");
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

  console.log("[WASM Mirror] About to await moduleFactory...");
  const factoryStart = performance.now();
  try {
    console.time("[TIMING] moduleFactory()");
    wasmModule = await moduleFactory(moduleOverrides);
    console.timeEnd("[TIMING] moduleFactory()");
  } catch (err) {
    console.error("[WASM Mirror] moduleFactory failed with error:", err);
    throw err;
  }
  const factoryTime = performance.now() - factoryStart;
  console.log(
    `[WASM Mirror] moduleFactory().COMPLETED after ${(factoryTime / 1000).toFixed(2)}s at ${new Date().toISOString()}`
  );

  if (!wasmModule) {
    throw new Error("Failed to load WASM module");
  }
  console.log("[WASM] Module loaded successfully");

  const factoryEnd = performance.now();
  console.log(`[WASM] TOTAL factory time: ${((factoryEnd - factoryStart) / 1000).toFixed(2)}s`);

  try {
    // Bind renderer functions to module if they exist
    const rendererFunctions = [
      '_ascii_renderer_init',
      '_ascii_renderer_render_frame',
      '_ascii_renderer_resize',
      '_ascii_renderer_get_cols',
      '_ascii_renderer_get_rows',
      '_ascii_renderer_shutdown',
    ];

    for (const funcName of rendererFunctions) {
      if (typeof (wasmModule as any)[funcName] === 'undefined') {
        console.warn(`[WASM] Renderer function not found: ${funcName}`);
      } else {
        console.log(`[WASM] Renderer function bound: ${funcName}`);
      }
    }

    // Initialize C options system with basic mirror mode arguments
    // This must be called before using any getter/setter functions
    if (wasmModule._mirror_init_with_args) {
      console.log(
        `[WASM] BEFORE _mirror_init_with_args at ${new Date().toISOString()}`
      );
      const initWithArgsStart = performance.now();
      console.log(
        "[WASM] Calling _mirror_init_with_args to initialize C options...",
      );
      // Pass JSON array of arguments: ["mirror"]
      const argsJson = '["mirror"]';
      const strLen = wasmModule.lengthBytesUTF8(argsJson) + 1;
      const strPtr = wasmModule._malloc(strLen);
      if (!strPtr) {
        throw new Error("Failed to allocate memory for args");
      }

      try {
        wasmModule.stringToUTF8(argsJson, strPtr, strLen);
        console.log(
          `[WASM] About to call _mirror_init_with_args at ${new Date().toISOString()}`
        );
        const callStart = performance.now();
        const initResult = wasmModule._mirror_init_with_args(strPtr);
        const callTime = performance.now() - callStart;
        const initWithArgsTime = performance.now() - initWithArgsStart;
        console.log(
          `[WASM] _mirror_init_with_args CALL TOOK ${callTime.toFixed(2)}ms, TOTAL ${initWithArgsTime.toFixed(2)}ms at ${new Date().toISOString()} with result ${initResult}`,
        );
        if (initResult !== 0) {
          console.error(
            "[WASM] _mirror_init_with_args failed with code:",
            initResult,
          );
          throw new Error(`Failed to initialize mirror C code: ${initResult}`);
        }
        console.log("[WASM] C options initialized successfully");
      } finally {
        const freeStart = performance.now();
        wasmModule._free(strPtr);
        const freeTime = performance.now() - freeStart;
        console.log(`[WASM] _free took ${freeTime.toFixed(2)}ms`);
      }
    } else {
      console.warn("[WASM] _mirror_init_with_args not found in WASM module");
    }

    console.log(
      `[WASM] About to createOptionAccessor at ${new Date().toISOString()}`
    );

    // Initialize shared options module with option accessor
    // This allows React components to call setColorMode, setPalette, etc.
    const optionsAccessorStart = performance.now();
    const optionsAccessor = createOptionAccessor(wasmModule);
    const optionsAccessorTime = performance.now() - optionsAccessorStart;
    console.log(
      `[WASM] createOptionAccessor DONE in ${optionsAccessorTime.toFixed(2)}ms at ${new Date().toISOString()}`,
    );

    console.log(
      `[WASM] About to initializeOptions at ${new Date().toISOString()}`
    );
    const initializeOptionsStart = performance.now();
    initializeOptions(optionsAccessor);
    const initializeOptionsTime = performance.now() - initializeOptionsStart;
    console.log(
      `[WASM] initializeOptions DONE in ${initializeOptionsTime.toFixed(2)}ms at ${new Date().toISOString()}`,
    );
    console.log("[WASM] Options module initialized");

    console.log(
      `[WASM] About to set globalWindow.asciiChatWasm at ${new Date().toISOString()}`
    );
    // Expose WASM module to window for JavaScript access (e.g., tooltips)
    const globalWindow = globalThis as typeof globalThis & {
      asciiChatWasm: AsciiChatWasmExports;
    };
    globalWindow.asciiChatWasm = {
      _wasmModule: wasmModule,
    };
    console.log(
      `[WASM] Set globalWindow.asciiChatWasm at ${new Date().toISOString()}`
    );

    console.log("[WASM] Initialization complete!", performance.now());

    // Force a microtask to check if something blocks after this
    Promise.resolve().then(() => {
      console.log("[WASM] After initialization microtask", performance.now());
    });
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
    console.log("[WASM] Cleaning up mirror module...");
    wasmModule._mirror_cleanup();
    wasmModule = null;
    cleanupOptions();
    console.log("[WASM] Mirror cleanup complete");
  } else {
    console.log("[WASM] Mirror cleanup: no module to clean up");
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

  frameCallCount++;

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
  } catch (err) {
    throw err;
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
