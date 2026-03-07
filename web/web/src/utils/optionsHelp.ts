/**
 * Wrapper for options_get_help_text() C API
 *
 * Provides TypeScript bindings to the C function that retrieves
 * help text for CLI options based on mode.
 */

// Import the WASM module (adjust path based on your build setup)
// This assumes the C code is compiled to WASM and available as a module

interface AsciiChatWasm {
  get_help_text(mode: number, optionName: number): number;
  _wasmModule?: {
    lengthBytesUTF8(str: string): number;
    stringToUTF8(str: string, outPtr: number, maxBytesToWrite: number): void;
    _malloc(size: number): number;
    _free(ptr: number): void;
    UTF8ToString(ptr: number): string;
  };
}

declare global {
  interface Window {
    asciiChatWasm?: AsciiChatWasm;
  }
}

/**
 * ASCII-chat mode constants (must match C definitions)
 */
export enum AsciiChatMode {
  SERVER = 0,
  CLIENT = 1,
  MIRROR = 2,
  DISCOVERY_SERVICE = 3,
  DISCOVERY = 4,
  INVALID = 5,
}

/**
 * Get help text for an option in a specific mode
 * @param mode The mode (use AsciiChatMode enum)
 * @param optionName The long name of the option (e.g., "color-mode", "fps")
 * @returns Help text string, or null if option doesn't apply to mode
 */
export function getHelpText(
  mode: AsciiChatMode,
  optionName: string,
): string | null {
  try {
    if (!window.asciiChatWasm?.get_help_text) {
      console.warn("WASM module not loaded, help text unavailable");
      return null;
    }

    // Allocate string in WASM memory for the option name
    const wasmModule = window.asciiChatWasm._wasmModule;
    if (!wasmModule) {
      console.warn("WASM module reference not available");
      return null;
    }

    const optionNameBytes = wasmModule.lengthBytesUTF8(optionName) + 1;
    const optionNamePtr = wasmModule._malloc(optionNameBytes);
    if (!optionNamePtr) {
      console.error("Failed to allocate memory for option name");
      return null;
    }

    try {
      wasmModule.stringToUTF8(optionName, optionNamePtr, optionNameBytes);

      // Call the WASM function
      const resultPtr = window.asciiChatWasm.get_help_text(mode, optionNamePtr);

      if (!resultPtr) {
        return null;
      }

      // Convert C string pointer to JavaScript string
      const result = wasmModule.UTF8ToString(resultPtr);
      return result || null;
    } finally {
      wasmModule._free(optionNamePtr);
    }
  } catch (error) {
    console.error(`Failed to get help text for ${optionName}:`, error);
    return null;
  }
}

/**
 * Type-safe wrapper for getting help text with a mode enum
 */
export function getOptionHelp(
  mode: AsciiChatMode,
  optionName: string,
): string | undefined {
  const help = getHelpText(mode, optionName);
  return help || undefined;
}
