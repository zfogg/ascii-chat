/**
 * @file optionsWrapper.ts
 * @brief Generic WASM option wrapper utilities
 *
 * Eliminates boilerplate for calling WASM option setters/getters by providing
 * a factory function that handles memory allocation, type conversion, and error handling.
 *
 * Each WASM module (mirror, client) exports unified option functions (set_width, get_width, etc.)
 * This wrapper provides type-safe TypeScript bindings that work with any WASM module.
 */

/**
 * WASM module interface with memory management and string functions
 */
export interface WasmModule {
  HEAPU8: Uint8Array;
  UTF8ToString(ptr: number): string;
  stringToUTF8(str: string, outPtr: number, maxBytes: number): void;
  lengthBytesUTF8(str: string): number;
  _malloc(size: number): number;
  _free(ptr: number): void;

  // Unified option functions (no mode prefix)
  _set_width?(value: number): number;
  _get_width?(): number;
  _set_height?(value: number): number;
  _get_height?(): number;
  _set_color_mode?(mode: number): number;
  _get_color_mode?(): number;
  _set_color_filter?(filter: number): number;
  _get_color_filter?(): number;
  _set_palette?(ptr: number): number;
  _get_palette?(): number;
  _set_palette_chars?(ptr: number): number;
  _get_palette_chars?(): number;
  _set_matrix_rain?(enabled: number): number;
  _get_matrix_rain?(): number;
  _set_flip_x?(enabled: number): number;
  _get_flip_x?(): number;
  _set_render_mode?(mode: number): number;
  _get_render_mode?(): number;
  _set_target_fps?(fps: number): number;
  _get_target_fps?(): number;
}

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
 * Handles memory management and type conversions transparently
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

  return {
    /**
     * Set an integer option
     * @param name The option name (e.g., 'width', 'color_mode')
     * @param value The value to set
     * @throws Error if the option doesn't exist or validation fails
     */
    setInt(name: string, value: number): void {
      const fn = (module as unknown as Record<string, unknown>)[`_set_${name}`];
      if (typeof fn !== "function") {
        throw new Error(
          `WASM function _set_${name} not found. Ensure module is loaded.`,
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
      const fn = (module as unknown as Record<string, unknown>)[`_get_${name}`];
      if (typeof fn !== "function") {
        throw new Error(
          `WASM function _get_${name} not found. Ensure module is loaded.`,
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
      const fn = (module as unknown as Record<string, unknown>)[`_set_${name}`];
      if (typeof fn !== "function") {
        throw new Error(
          `WASM function _set_${name} not found. Ensure module is loaded.`,
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
      const fn = (module as unknown as Record<string, unknown>)[`_get_${name}`];
      if (typeof fn !== "function") {
        throw new Error(
          `WASM function _get_${name} not found. Ensure module is loaded.`,
        );
      }
      return (fn as () => number).call(module) !== 0;
    },

    /**
     * Set a string option (handles memory allocation)
     * @param name The option name
     * @param value The string value to set
     * @throws Error if allocation fails or validation fails
     */
    setString(name: string, value: string): void {
      const fn = (module as unknown as Record<string, unknown>)[`_set_${name}`];
      if (typeof fn !== "function") {
        throw new Error(
          `WASM function _set_${name} not found. Ensure module is loaded.`,
        );
      }

      if (!value) {
        throw new Error(`${name} cannot be empty`);
      }

      const strLen = module.lengthBytesUTF8(value) + 1;
      const strPtr = module._malloc(strLen);
      if (!strPtr) {
        throw new Error(`Failed to allocate memory for ${name}`);
      }

      try {
        module.stringToUTF8(value, strPtr, strLen);
        const result = (fn as (ptr: number) => number).call(module, strPtr);
        if (result !== 0) {
          throw new Error(`Failed to set ${name}: ${value}`);
        }
      } finally {
        module._free(strPtr);
      }
    },

    /**
     * Get a string option (reads from WASM memory)
     * @param name The option name
     * @returns The current string value
     * @throws Error if the option doesn't exist
     */
    getString(name: string): string {
      const fn = (module as unknown as Record<string, unknown>)[`_get_${name}`];
      if (typeof fn !== "function") {
        throw new Error(
          `WASM function _get_${name} not found. Ensure module is loaded.`,
        );
      }

      const ptr = (fn as () => number).call(module);
      if (!ptr) return "";

      return module.UTF8ToString(ptr);
    },
  };
}
