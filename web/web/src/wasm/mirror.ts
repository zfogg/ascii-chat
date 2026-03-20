// TypeScript wrapper for Mirror WASM module
// Provides type-safe interface to libasciichat mirror mode

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
  _malloc(size: number): number;
  _free(ptr: number): void;
}

interface MirrorModule extends WasmModule {
  _mirror_init_with_args?: MirrorModuleExports["_mirror_init_with_args"];
  _mirror_cleanup: MirrorModuleExports["_mirror_cleanup"];
  _mirror_convert_frame: MirrorModuleExports["_mirror_convert_frame"];
  _mirror_free_string: MirrorModuleExports["_mirror_free_string"];
  _get_help_text: MirrorModuleExports["_get_help_text"];
}

// Re-export enums and types from shared options module
export { RenderMode, ColorMode, ColorFilter };
export type { Palette };

// Import the Emscripten-generated module factory
// @ts-expect-error - Generated file without types
import MirrorModuleFactory from "./dist/mirror.js";

let wasmModule: MirrorModule | null = null;
let frameCallCount = 0;

/**
 * Initialize the WASM module (call once at app start)
 */
export async function initMirrorWasm(): Promise<void> {
  if (wasmModule) return;

  console.log("[WASM] Starting module factory...");
  // Provide runtime environment functions for Emscripten
  wasmModule = await MirrorModuleFactory({
    // libsodium crypto random - returns 32-bit unsigned integer
    getRandomValue: function () {
      const buf = new Uint32Array(1);
      crypto.getRandomValues(buf);
      return buf[0];
    },
    // Forward C stdout to browser console
    print: (text: string) => {
      console.log("[C] " + text);
    },
    // Forward C stderr to browser console
    printErr: (text: string) => {
      console.error("[C] " + text);
    },
  });
  console.log("[WASM] Module factory completed");

  if (!wasmModule) {
    throw new Error("Failed to load WASM module");
  }
  console.log("[WASM] Module loaded successfully");

  try {
    // Initialize C options system with basic mirror mode arguments
    // This must be called before using any getter/setter functions
    if (wasmModule._mirror_init_with_args) {
      console.log("[WASM] Calling _mirror_init_with_args to initialize C options...");
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
          console.error(
            "[WASM] _mirror_init_with_args failed with code:",
            initResult,
          );
          throw new Error(
            `Failed to initialize mirror C code: ${initResult}`,
          );
        }
        console.log("[WASM] C options initialized successfully");
      } finally {
        wasmModule._free(strPtr);
      }
    } else {
      console.warn("[WASM] _mirror_init_with_args not found in WASM module");
    }

    // Initialize shared options module with option accessor
    // This allows React components to call setColorMode, setPalette, etc.
    const optionsAccessor = createOptionAccessor(wasmModule);
    initializeOptions(optionsAccessor);
    console.log("[WASM] Options module initialized");

    // Expose WASM module to window for JavaScript access (e.g., tooltips)
    const globalWindow = globalThis as typeof globalThis & {
      asciiChatWasm: AsciiChatWasmExports;
    };
    globalWindow.asciiChatWasm = {
      _wasmModule: wasmModule,
    };

    console.log("[WASM] Initialization complete!");
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
  if (frameCallCount % 300 === 0) {
    console.log(`[WASM] Processed ${frameCallCount} frames`);
  }

  // Allocate memory for RGBA data
  console.log(`[WASM] Requesting malloc for ${rgbaData.length} bytes`);
  const dataPtr = wasmModule._malloc(rgbaData.length);

  // CRITICAL: In WASM, malloc returns 0 on failure (0 is a valid memory address but indicates error)
  // Accessing memory at address 0 would corrupt the heap
  if (dataPtr === 0 || dataPtr === null || typeof dataPtr === "undefined") {
    console.error(
      `[WASM] CRITICAL: malloc returned ${dataPtr} for ${rgbaData.length} bytes. WASM heap may be exhausted.`,
    );
    throw new Error(
      `Failed to allocate WASM memory for RGBA data (${rgbaData.length} bytes). WASM heap may be exhausted.`,
    );
  }

  if (dataPtr < 0) {
    console.error(`[WASM] malloc returned negative pointer: ${dataPtr}`);
    throw new Error(`malloc returned invalid pointer: ${dataPtr}`);
  }

  console.log(`[WASM] malloc succeeded: dataPtr=${dataPtr}`);

  // Also check that we're not at the start of memory (0 is often heap metadata)
  if (dataPtr < 1024) {
    console.warn(
      `[WASM] WARNING: malloc returned very low address (${dataPtr}), possible heap corruption risk`,
    );
  }

  try {
    // Validate RGBA data before copying to WASM
    if (!rgbaData || rgbaData.length === 0) {
      throw new Error(
        `Invalid RGBA data: buffer is ${!rgbaData ? "null/undefined" : "empty"}`,
      );
    }

    // Verify expected size matches actual size
    const expectedSize = srcWidth * srcHeight * 4;
    if (rgbaData.length !== expectedSize) {
      console.error(
        `[WASM] CRITICAL: RGBA data size mismatch before copy - expected ${expectedSize}, got ${rgbaData.length}`,
      );
    }

    // Copy RGBA data to WASM memory - catch any errors during copy
    let copyError: Error | null = null;
    try {
      console.log(
        `[WASM] Starting HEAPU8.set: copying ${rgbaData.length} bytes to WASM memory at ptr=${dataPtr}`,
      );
      wasmModule.HEAPU8.set(rgbaData, dataPtr);
      console.log(`[WASM] HEAPU8.set completed successfully`);
    } catch (err) {
      copyError = err as Error;
      console.error(`[WASM] ERROR during HEAPU8.set: ${copyError.message}`);
      // Re-throw with additional context already logged above
      throw copyError;
    }

    // Verify data was copied correctly by spot-checking bytes
    const firstByteAfterCopy = wasmModule.HEAPU8[dataPtr];
    const lastByteAfterCopy = wasmModule.HEAPU8[dataPtr + rgbaData.length - 1];
    const lastExpectedPtr = dataPtr + rgbaData.length - 1;

    console.log(
      `[WASM] Frame ${frameCallCount}: dimensions=${srcWidth}x${srcHeight}, buffer=${rgbaData.length} bytes, dataPtr=${dataPtr}, lastBytePtr=${lastExpectedPtr}, firstByte=${firstByteAfterCopy}, lastByte=${lastByteAfterCopy}`,
    );

    // Validate the C function pointers are available
    if (!wasmModule._mirror_convert_frame) {
      throw new Error("WASM function _mirror_convert_frame not found");
    }

    console.log(
      `[WASM] About to call _mirror_convert_frame(dataPtr=${dataPtr}, srcWidth=${srcWidth}, srcHeight=${srcHeight})`,
    );

    // Call WASM function (dimensions come from options set during init)
    let resultPtr: number;
    try {
      resultPtr = wasmModule._mirror_convert_frame(
        dataPtr,
        srcWidth,
        srcHeight,
      );
      console.log(`[WASM] _mirror_convert_frame returned: ${resultPtr}`);
    } catch (err) {
      const callError = err as Error;
      console.error(
        `[WASM] FATAL ERROR calling _mirror_convert_frame: ${callError.message}`,
      );
      console.error(
        `[WASM] Context: srcWidth=${srcWidth}, srcHeight=${srcHeight}, dataPtr=${dataPtr}, bufferSize=${rgbaData.length}`,
      );
      // Re-throw with context already logged above
      throw callError;
    }

    if (!resultPtr) {
      console.error(
        `[WASM] mirror_convert_frame returned NULL pointer for frame ${frameCallCount}`,
      );
      throw new Error(
        `WASM mirror_convert_frame returned null after processing ${srcWidth}x${srcHeight}`,
      );
    }

    let asciiString: string;
    try {
      // Convert C string to JavaScript string
      asciiString = wasmModule.UTF8ToString(resultPtr);

      // Debug: log string length on palette change
      if (frameCallCount % 30 === 0) {
        console.log(
          `[WASM] Frame result: ptr=${resultPtr}, length=${asciiString.length}, first 50 chars:`,
          asciiString.substring(0, 50),
        );
      }
    } finally {
      // Always free the result buffer (allocated by WASM), even if UTF8ToString fails
      wasmModule._mirror_free_string(resultPtr);
    }

    return asciiString;
  } catch (err) {
    console.error("[WASM] convertFrameToAscii error:", err);
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
