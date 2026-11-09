/**
 * Wrapper for options_get_help_text() C API
 *
 * Provides TypeScript bindings to the C function that retrieves
 * help text for CLI options based on mode.
 */

import { getMirrorModule } from "@ascii-chat/shared/wasm";
import { getClientWasmModule } from "../wasm/client";

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
    const module = getMirrorModule() ?? getClientWasmModule();
    if (!module) {
      console.warn("WASM module not loaded, help text unavailable");
      return null;
    }

    if (typeof module.optionHelp !== "function") return null;
    return module.optionHelp(mode, optionName) || null;
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
