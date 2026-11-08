import {
  ColorMode as WasmColorMode,
  ColorFilter as WasmColorFilter,
} from "@ascii-chat/shared/wasm";
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
