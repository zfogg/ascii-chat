import {
  ColorMode as WasmColorMode,
  ColorFilter as WasmColorFilter,
  colorModeValues,
  colorFilterValues,
} from "@ascii-chat/shared/wasm";
import type { ColorMode, ColorFilter } from "../components";

/**
 * Map Settings ColorMode to WASM mirror module enum
 */
export function mapColorModeToWasm(mode: ColorMode): WasmColorMode {
  return colorModeValues[mode];
}

/**
 * Map Settings ColorFilter to WASM mirror module enum
 */
export function mapColorFilterToWasm(filter: ColorFilter): WasmColorFilter {
  return colorFilterValues[filter];
}
