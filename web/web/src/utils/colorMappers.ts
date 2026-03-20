import {
  ColorMode as WasmColorMode,
  ColorFilter as WasmColorFilter,
} from "../wasm/mirror";
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
