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
    auto: WasmColorMode.Auto,
    none: WasmColorMode.None,
    "16": WasmColorMode.Ansi16,
    "256": WasmColorMode.Ansi256,
    truecolor: WasmColorMode.Truecolor,
  };
  return mapping[mode];
}

/**
 * Map Settings ColorFilter to WASM mirror module enum
 */
export function mapColorFilterToWasm(filter: ColorFilter): WasmColorFilter {
  const mapping: Record<ColorFilter, WasmColorFilter> = {
    none: WasmColorFilter.None,
    black: WasmColorFilter.Black,
    white: WasmColorFilter.White,
    green: WasmColorFilter.Green,
    magenta: WasmColorFilter.Magenta,
    fuchsia: WasmColorFilter.Fuchsia,
    orange: WasmColorFilter.Orange,
    teal: WasmColorFilter.Teal,
    cyan: WasmColorFilter.Cyan,
    pink: WasmColorFilter.Pink,
    red: WasmColorFilter.Red,
    yellow: WasmColorFilter.Yellow,
    rainbow: WasmColorFilter.Rainbow,
  };
  return mapping[filter];
}
