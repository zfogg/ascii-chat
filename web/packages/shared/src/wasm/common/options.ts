/** Rendering values shared by browser consumers. */
export enum RenderMode {
  FOREGROUND = 0,
  BACKGROUND = 1,
  HALF_BLOCK = 2,
}

export enum ColorMode {
  AUTO = -1,
  NONE = 0,
  COLOR_16 = 1,
  COLOR_256 = 2,
  TRUECOLOR = 3,
}

export enum ColorFilter {
  NONE = 0,
  BLACK = 1,
  WHITE = 2,
  GREEN = 3,
  MAGENTA = 4,
  FUCHSIA = 5,
  ORANGE = 6,
  TEAL = 7,
  CYAN = 8,
  PINK = 9,
  RED = 10,
  YELLOW = 11,
  RAINBOW = 12,
}

export {
  paletteTypes,
  paletteName,
} from "../../../../../../src/web/common/options";
export type { Palette } from "../../../../../../src/web/common/options";
