import {
  ColorMode as WasmColorMode,
  ColorFilter as WasmColorFilter,
} from "@ascii-chat/shared/wasm";
import {
  ColorMode as ClientColorMode,
  ColorFilter as ClientColorFilter,
} from "../wasm/client";
import type { ColorMode, ColorFilter } from "../components";

/**
 * Map Settings ColorMode to WASM mirror module enum
 */
export function mapColorModeToWasm(mode: ColorMode): WasmColorMode {
  const mapping: Record<ColorMode, WasmColorMode> = {
    auto: WasmColorMode.AUTO,
    none: WasmColorMode.NONE,
    "16": WasmColorMode.COLOR_16,
    "256": WasmColorMode.COLOR_256,
    truecolor: WasmColorMode.TRUECOLOR,
  };
  return mapping[mode];
}

/**
 * Map Settings ColorFilter to WASM mirror module enum
 */
export function mapColorFilterToWasm(filter: ColorFilter): WasmColorFilter {
  const mapping: Record<ColorFilter, WasmColorFilter> = {
    none: WasmColorFilter.NONE,
    black: WasmColorFilter.BLACK,
    white: WasmColorFilter.WHITE,
    green: WasmColorFilter.GREEN,
    magenta: WasmColorFilter.MAGENTA,
    fuchsia: WasmColorFilter.FUCHSIA,
    orange: WasmColorFilter.ORANGE,
    teal: WasmColorFilter.TEAL,
    cyan: WasmColorFilter.CYAN,
    pink: WasmColorFilter.PINK,
    red: WasmColorFilter.RED,
    yellow: WasmColorFilter.YELLOW,
    rainbow: WasmColorFilter.RAINBOW,
  };
  return mapping[filter];
}

/**
 * Map Settings ColorMode to WASM client module enum
 */
export function mapColorModeToClient(mode: ColorMode): ClientColorMode {
  const mapping: Record<ColorMode, ClientColorMode> = {
    auto: ClientColorMode.AUTO,
    none: ClientColorMode.NONE,
    "16": ClientColorMode.COLOR_16,
    "256": ClientColorMode.COLOR_256,
    truecolor: ClientColorMode.TRUECOLOR,
  };
  return mapping[mode] || ClientColorMode.AUTO;
}

/**
 * Map Settings ColorFilter to WASM client module enum
 */
export function mapColorFilterToClient(filter: ColorFilter): ClientColorFilter {
  const mapping: Record<ColorFilter, ClientColorFilter> = {
    none: ClientColorFilter.NONE,
    black: ClientColorFilter.BLACK,
    white: ClientColorFilter.WHITE,
    green: ClientColorFilter.GREEN,
    magenta: ClientColorFilter.MAGENTA,
    fuchsia: ClientColorFilter.FUCHSIA,
    orange: ClientColorFilter.ORANGE,
    teal: ClientColorFilter.TEAL,
    cyan: ClientColorFilter.CYAN,
    pink: ClientColorFilter.PINK,
    red: ClientColorFilter.RED,
    yellow: ClientColorFilter.YELLOW,
    rainbow: ClientColorFilter.RAINBOW,
  };
  return mapping[filter] || ClientColorFilter.NONE;
}

/**
 * Reverse map: WASM color mode enum to string
 */
export function wasmColorModeToString(mode: WasmColorMode): ColorMode {
  const mapping: Record<WasmColorMode, ColorMode> = {
    [WasmColorMode.AUTO]: "auto",
    [WasmColorMode.NONE]: "none",
    [WasmColorMode.COLOR_16]: "16",
    [WasmColorMode.COLOR_256]: "256",
    [WasmColorMode.TRUECOLOR]: "truecolor",
  };
  return mapping[mode] || "auto";
}

/**
 * Reverse map: WASM color filter enum to string
 */
export function wasmColorFilterToString(filter: WasmColorFilter): ColorFilter {
  const mapping: Record<WasmColorFilter, ColorFilter> = {
    [WasmColorFilter.NONE]: "none",
    [WasmColorFilter.BLACK]: "black",
    [WasmColorFilter.WHITE]: "white",
    [WasmColorFilter.GREEN]: "green",
    [WasmColorFilter.MAGENTA]: "magenta",
    [WasmColorFilter.FUCHSIA]: "fuchsia",
    [WasmColorFilter.ORANGE]: "orange",
    [WasmColorFilter.TEAL]: "teal",
    [WasmColorFilter.CYAN]: "cyan",
    [WasmColorFilter.PINK]: "pink",
    [WasmColorFilter.RED]: "red",
    [WasmColorFilter.YELLOW]: "yellow",
    [WasmColorFilter.RAINBOW]: "rainbow",
  };
  return mapping[filter] || "none";
}
