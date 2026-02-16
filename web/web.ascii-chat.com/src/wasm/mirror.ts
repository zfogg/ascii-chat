// TypeScript wrapper for Mirror WASM module
// Provides type-safe interface to libasciichat mirror mode

import { createOptionAccessor, type WasmModule } from "./common/optionsWrapper";
import {
  initializeSettings,
  cleanupSettings,
  RenderMode,
  ColorMode,
  ColorFilter,
  type Palette,
} from "./settings";

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
  _mirror_init_with_args: MirrorModuleExports["_mirror_init_with_args"];
  _mirror_cleanup: MirrorModuleExports["_mirror_cleanup"];
  _mirror_convert_frame: MirrorModuleExports["_mirror_convert_frame"];
  _mirror_free_string: MirrorModuleExports["_mirror_free_string"];
  _get_help_text: MirrorModuleExports["_get_help_text"];
}

// Re-export enums and types from shared settings module
export { RenderMode, ColorMode, ColorFilter };
export type { Palette };

// Import the Emscripten-generated module factory
// @ts-expect-error - Generated file without types
import MirrorModuleFactory from "./dist/mirror.js";

let wasmModule: MirrorModule | null = null;
let frameCallCount = 0;

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
  [ColorMode.AUTO]: "auto",
  [ColorMode.NONE]: "none",
  [ColorMode.COLOR_16]: "16",
  [ColorMode.COLOR_256]: "256",
  [ColorMode.TRUECOLOR]: "truecolor",
};

const colorFilterNames: Record<ColorFilter, string> = {
  [ColorFilter.NONE]: "none",
  [ColorFilter.BLACK]: "black",
  [ColorFilter.WHITE]: "white",
  [ColorFilter.GREEN]: "green",
  [ColorFilter.MAGENTA]: "magenta",
  [ColorFilter.FUCHSIA]: "fuchsia",
  [ColorFilter.ORANGE]: "orange",
  [ColorFilter.TEAL]: "teal",
  [ColorFilter.CYAN]: "cyan",
  [ColorFilter.PINK]: "pink",
  [ColorFilter.RED]: "red",
  [ColorFilter.YELLOW]: "yellow",
  [ColorFilter.RAINBOW]: "rainbow",
};

const renderModeNames: Record<RenderMode, string> = {
  [RenderMode.FOREGROUND]: "foreground",
  [RenderMode.BACKGROUND]: "background",
  [RenderMode.HALF_BLOCK]: "half-block",
};

/**
 * Initialize the WASM module (call once at app start)
 */
export async function initMirrorWasm(
  options: MirrorInitOptions = {},
): Promise<void> {
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

  // Build argument string for options_init()
  const args: string[] = ["mirror"];

  if (options.width !== undefined) {
    args.push("--width", options.width.toString());
  }
  if (options.height !== undefined) {
    args.push("--height", options.height.toString());
  }
  if (options.colorMode !== undefined) {
    args.push("--color-mode", colorModeNames[options.colorMode]);
  }
  if (options.colorFilter !== undefined) {
    args.push("--color-filter", colorFilterNames[options.colorFilter]);
  }
  if (options.renderMode !== undefined) {
    args.push("--render-mode", renderModeNames[options.renderMode]);
  }
  if (options.palette !== undefined) {
    args.push("--palette", options.palette);
  }

  const argsString = args.join(" ");
  console.log("[WASM] Initializing with args:", argsString);

  // Allocate string in WASM memory
  const strLen = wasmModule.lengthBytesUTF8(argsString) + 1;
  const strPtr = wasmModule._malloc(strLen);
  if (!strPtr) {
    throw new Error("Failed to allocate memory for args string");
  }

  try {
    wasmModule.stringToUTF8(argsString, strPtr, strLen);

    console.log("[WASM] Calling _mirror_init_with_args...");
    // Initialize libasciichat with parsed arguments
    const result = wasmModule._mirror_init_with_args(strPtr);
    console.log("[WASM] _mirror_init_with_args returned:", result);
    if (result !== 0) {
      throw new Error("Failed to initialize mirror WASM module");
    }
    console.log("[WASM] Initialization complete!");

    // Initialize shared settings module with option accessor
    const optionsAccessor = createOptionAccessor(wasmModule);
    initializeSettings(optionsAccessor);

    // Expose WASM module to window for JavaScript access (e.g., tooltips)
    const globalWindow = globalThis as typeof globalThis & {
      asciiChatWasm: AsciiChatWasmExports;
    };
    globalWindow.asciiChatWasm = {
      _wasmModule: wasmModule,
      get_help_text: wasmModule._get_help_text.bind(wasmModule),
    };
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
    cleanupSettings();
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

  frameCallCount++;
  if (frameCallCount % 300 === 0) {
    console.log(`[WASM] Processed ${frameCallCount} frames`);
  }

  // Allocate WASM memory for input RGBA data
  const dataPtr = wasmModule._malloc(rgbaData.length);
  if (!dataPtr) {
    throw new Error(
      `Failed to allocate WASM memory for RGBA data (${rgbaData.length} bytes)`,
    );
  }

  try {
    // Copy RGBA data to WASM memory
    wasmModule.HEAPU8.set(rgbaData, dataPtr);

    // Call WASM function (dimensions come from options set during init)
    const resultPtr = wasmModule._mirror_convert_frame(
      dataPtr,
      srcWidth,
      srcHeight,
    );

    if (!resultPtr) {
      console.error("[WASM] mirror_convert_frame returned NULL pointer");
      throw new Error("WASM mirror_convert_frame returned null");
    }

    // Convert C string to JavaScript string
    const asciiString = wasmModule.UTF8ToString(resultPtr);

    // Debug: log string length on palette change
    if (frameCallCount % 30 === 0) {
      console.log(
        `[WASM] Frame result: ptr=${resultPtr}, length=${asciiString.length}, first 50 chars:`,
        asciiString.substring(0, 50),
      );
    }

    // Free the result buffer (allocated by WASM)
    wasmModule._mirror_free_string(resultPtr);

    return asciiString;
  } catch (err) {
    console.error("[WASM] convertFrameToAscii error:", err);
    throw err;
  } finally {
    // Always free the input buffer
    wasmModule._free(dataPtr);
  }
}

/**
 * Check if WASM module is ready
 */
export function isWasmReady(): boolean {
  return wasmModule !== null && wasmModule.HEAPU8 !== undefined;
}
