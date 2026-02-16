/**
 * Wrapper for options_get_help_text() C API
 *
 * Provides TypeScript bindings to the C function that retrieves
 * help text for CLI options based on mode.
 */

// Import the WASM module (adjust path based on your build setup)
// This assumes the C code is compiled to WASM and available as a module
declare global {
  interface Window {
    asciiChatWasm?: {
      getHelpText(mode: number, optionName: string): string | null;
    };
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
  optionName: string
): string | null {
  try {
    if (!window.asciiChatWasm?.getHelpText) {
      console.warn("WASM module not loaded, help text unavailable");
      return null;
    }

    const result = window.asciiChatWasm.getHelpText(mode, optionName);
    return result || null;
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
  optionName: string
): string | undefined {
  const help = getHelpText(mode, optionName);
  return help || undefined;
}
