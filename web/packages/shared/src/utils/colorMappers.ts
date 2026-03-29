import type { ColorMode, ColorFilter } from "../wasm/common/options";

/**
 * Reverse map: WASM color mode enum to string for CLI arguments
 */
export function wasmColorModeToString(mode: ColorMode): string {
  const mapping: Record<ColorMode, string> = {
    [-1]: "auto",
    [0]: "none",
    [1]: "16",
    [2]: "256",
    [3]: "truecolor",
  };
  return mapping[mode] ?? "auto";
}

/**
 * Reverse map: WASM color filter enum to string for CLI arguments
 */
export function wasmColorFilterToString(filter: ColorFilter): string {
  const mapping: Record<ColorFilter, string> = {
    [0]: "none",
    [1]: "black",
    [2]: "white",
    [3]: "green",
    [4]: "magenta",
    [5]: "fuchsia",
    [6]: "orange",
    [7]: "teal",
    [8]: "cyan",
    [9]: "pink",
    [10]: "red",
    [11]: "yellow",
    [12]: "rainbow",
  };
  return mapping[filter] ?? "none";
}
