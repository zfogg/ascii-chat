import type { RuntimeOptionsModule } from "./runtimeOptions";
import type { MainModule } from "../generated/mirror-bindings";
/**
 * @file optionsWrapper.ts
 * @brief Generic WASM option wrapper utilities
 *
 * Provides typed access to the Embind option functions and converts boolean values.
 *
 * Each WASM module (mirror, client) exports unified option functions (set_width, get_width, etc.)
 * This wrapper provides type-safe TypeScript bindings that work with any WASM module.
 */

/** TypeScript signatures generated from the shared Embind options interface. */
export type WasmModule = RuntimeOptionsModule &
  Pick<
    MainModule,
    | "setPalette"
    | "setPaletteChars"
    | "getPaletteChars"
    | "set_width"
    | "get_width"
    | "set_height"
    | "get_height"
    | "set_color_mode"
    | "get_color_mode"
    | "set_color_filter"
    | "get_color_filter"
    | "set_matrix_rain"
    | "get_matrix_rain"
    | "set_waveform"
    | "get_waveform"
    | "set_fft"
    | "get_fft"
    | "set_flip_x"
    | "get_flip_x"
    | "set_flip_y"
    | "get_flip_y"
    | "set_render_mode"
    | "get_render_mode"
    | "set_target_fps"
    | "get_target_fps"
    | "get_palette"
  > & { HEAPU8: Uint8Array };

/**
 * Type-safe option accessor interface
 */
export interface OptionAccessor {
  setInt(name: string, value: number): void;
  getInt(name: string): number;
  setBool(name: string, value: boolean): void;
  getBool(name: string): boolean;
  setString(name: string, value: string): void;
  getString(name: string): string;
}

/**
 * Create a type-safe option accessor for a WASM module
 * Handles option type conversions and errors.
 *
 * Usage:
 *   const options = createOptionAccessor(wasmModule);
 *   options.setInt('width', 640);
 *   const width = options.getInt('width');
 *   options.setString('palette_chars', '@#%*+=-:. ');
 *
 * @param module The WASM module instance
 * @returns Option accessor with type-safe methods
 */
export function createOptionAccessor(module: WasmModule): OptionAccessor {
  if (!module) {
    throw new Error("WASM module is required");
  }

  const accessor = {
    /**
     * Set an integer option
     * @param name The option name (e.g., 'width', 'color_mode')
     * @param value The value to set
     * @throws Error if the option doesn't exist or validation fails
     */
    setInt(name: string, value: number): void {
      const fn = (module as unknown as Record<string, unknown>)[`set_${name}`];
      if (typeof fn !== "function") {
        throw new Error(
          `WASM function set_${name} not found. Ensure module is loaded.`,
        );
      }
      const result = (fn as (v: number) => number).call(module, value);
      if (result !== 0) {
        throw new Error(`Failed to set ${name}: ${value}`);
      }
    },

    /**
     * Get an integer option
     * @param name The option name
     * @returns The current value
     * @throws Error if the option doesn't exist
     */
    getInt(name: string): number {
      const fn = (module as unknown as Record<string, unknown>)[`get_${name}`];
      if (typeof fn !== "function") {
        throw new Error(
          `WASM function get_${name} not found. Ensure module is loaded.`,
        );
      }
      return (fn as () => number).call(module);
    },

    /**
     * Set a boolean option
     * @param name The option name
     * @param value The value to set
     * @throws Error if the option doesn't exist or validation fails
     */
    setBool(name: string, value: boolean): void {
      const fn = (module as unknown as Record<string, unknown>)[`set_${name}`];
      if (typeof fn !== "function") {
        throw new Error(
          `WASM function set_${name} not found. Ensure module is loaded.`,
        );
      }
      const result = (fn as (v: number) => number).call(module, value ? 1 : 0);
      if (result !== 0) {
        throw new Error(`Failed to set ${name}: ${value}`);
      }
    },

    /**
     * Get a boolean option
     * @param name The option name
     * @returns The current value
     * @throws Error if the option doesn't exist
     */
    getBool(name: string): boolean {
      const fn = (module as unknown as Record<string, unknown>)[`get_${name}`];
      if (typeof fn !== "function") {
        throw new Error(
          `WASM function get_${name} not found. Ensure module is loaded.`,
        );
      }
      return (fn as () => number).call(module) !== 0;
    },

    /**
     * Set a string option
     * @param name The option name
     * @param value The string value to set
     * @throws Error if allocation fails or validation fails
     */
    setString(name: string, value: string): void {
      if (!value) {
        throw new Error(`${name} cannot be empty`);
      }
      const fn =
        name === "palette"
          ? module.setPalette
          : name === "palette_chars"
            ? module.setPaletteChars
            : null;
      if (!fn) throw new Error(`WASM string option ${name} not found`);
      if (fn(value) !== 0) throw new Error(`Failed to set ${name}: ${value}`);
    },

    /**
     * Get a string option
     * @param name The option name
     * @returns The current string value
     * @throws Error if the option doesn't exist
     */
    getString(name: string): string {
      if (name !== "palette_chars")
        throw new Error(`WASM string option ${name} not found`);
      return module.getPaletteChars();
    },
  };
  return accessor;
}
