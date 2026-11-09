/** Values match the native terminal and ASCII renderer enums. */
export const ColorMode = {
  Auto: -1,
  None: 0,
  Ansi16: 1,
  Ansi256: 2,
  Truecolor: 3,
} as const;
export type ColorMode = (typeof ColorMode)[keyof typeof ColorMode];
export const RenderMode = {
  Foreground: 0,
  Background: 1,
  HalfBlock: 2,
} as const;
export type RenderMode = (typeof RenderMode)[keyof typeof RenderMode];
export const ColorFilter = {
  None: 0,
  Black: 1,
  White: 2,
  Green: 3,
  Magenta: 4,
  Fuchsia: 5,
  Orange: 6,
  Teal: 7,
  Cyan: 8,
  Pink: 9,
  Red: 10,
  Yellow: 11,
  Rainbow: 12,
} as const;
export type ColorFilter = (typeof ColorFilter)[keyof typeof ColorFilter];
export const TextRenderer = { Luminance: 0, Glyph: 1 } as const;
export type TextRenderer = (typeof TextRenderer)[keyof typeof TextRenderer];
export const AudioSource = { All: 0, Call: 1, Mic: 2, Media: 3 } as const;
export type AudioSource = (typeof AudioSource)[keyof typeof AudioSource];
/** Built-in luminance palettes, ordered from dark to bright, matching palette.h. */
export const defaultPalettes = {
  standard: "   ...',;:clodxkO0KXNWM",
  blocks: "   ░░▒▒▓▓██",
  digital: "   -=≡≣▰▱◼",
  minimal: "   .-+*#",
  cool: "   ▁▂▃▄▅▆▇█",
} as const;

/** Browser-facing names map to the native numeric values above. */
export const colorModeValues = {
  auto: ColorMode.Auto,
  none: ColorMode.None,
  "16": ColorMode.Ansi16,
  "256": ColorMode.Ansi256,
  truecolor: ColorMode.Truecolor,
} as const;
export const colorFilterValues = {
  none: ColorFilter.None,
  black: ColorFilter.Black,
  white: ColorFilter.White,
  green: ColorFilter.Green,
  magenta: ColorFilter.Magenta,
  fuchsia: ColorFilter.Fuchsia,
  orange: ColorFilter.Orange,
  teal: ColorFilter.Teal,
  cyan: ColorFilter.Cyan,
  pink: ColorFilter.Pink,
  red: ColorFilter.Red,
  yellow: ColorFilter.Yellow,
  rainbow: ColorFilter.Rainbow,
} as const;
export const defaultBrowserSettings = {
  colorMode: "truecolor",
  colorFilter: "none",
  palette: "standard",
  renderer: "luminance",
  renderMode: RenderMode.Foreground,
  paletteChars: defaultPalettes.standard,
} as const;

/** Values accepted by the native options registry. */
export type OptionValue = number | string | boolean;
export type OptionValues = Readonly<Record<string, OptionValue>>;
export interface OptionsSnapshot {
  revision: number;
  values: OptionValues;
}
/** Shared Embind options interface, implemented by each browser WASM module. */
export interface OptionsBindings {
  OptionsBinding: {
    revision(): number;
    read(): OptionsSnapshot | null;
    patch(values: OptionValues, expectedRevision: number): number;
  };
}
export const paletteTypes = {
  standard: 1,
  blocks: 2,
  digital: 3,
  minimal: 4,
  cool: 5,
  custom: 6,
} as const;
export type Palette = keyof typeof paletteTypes;
const paletteNames: readonly Palette[] = [
  "standard",
  "standard",
  "blocks",
  "digital",
  "minimal",
  "cool",
  "custom",
];
export function paletteName(type: number): Palette {
  const name = paletteNames[type];
  if (!name) throw new Error(`Invalid palette type: ${type}`);
  return name;
}

/** The native options buffer reserves one byte for its terminator. */
export const MAX_CUSTOM_PALETTE_BYTES = 255;

/** Validate a patch before it is submitted to the atomic native options API. */
export function validateOptions(values: OptionValues): void {
  const enums: Record<string, readonly number[]> = {
    color_mode: Object.values(ColorMode),
    render_mode: Object.values(RenderMode),
    color_filter: Object.values(ColorFilter),
    renderer: Object.values(TextRenderer),
    palette_type: [0, ...Object.values(paletteTypes)],
    audio_source: Object.values(AudioSource),
  };
  for (const [name, valid] of Object.entries(enums)) {
    if (Object.hasOwn(values, name) && !valid.includes(values[name] as number))
      throw new Error(`Invalid ${name}: ${String(values[name])}`);
  }
  if (Object.hasOwn(values, "palette_custom")) {
    const palette = values["palette_custom"];
    if (typeof palette !== "string")
      throw new Error("Custom palette must be a string");
    if (Array.from(palette).length > 1024)
      throw new Error("Custom palette cannot exceed 1024 characters");
    if (new TextEncoder().encode(palette).length > MAX_CUSTOM_PALETTE_BYTES)
      throw new Error(
        `Custom palette cannot exceed ${MAX_CUSTOM_PALETTE_BYTES} UTF-8 bytes`,
      );
  }
}
/** Applies a validated patch to the native options state atomically. */
export class Options {
  constructor(private readonly bindings: OptionsBindings) {}
  read(): OptionsSnapshot {
    const snapshot = this.bindings.OptionsBinding.read();
    if (snapshot === null) throw new Error("Unable to read renderer options");
    validateOptions(snapshot.values);
    return snapshot;
  }
  patch(values: OptionValues, expectedRevision = -1): OptionsSnapshot {
    validateOptions(values);
    const code = this.bindings.OptionsBinding.patch(values, expectedRevision);
    if (code !== 0)
      throw new Error(
        `Settings update rejected (${code}); refresh the settings and retry.`,
      );
    return this.read();
  }
  palette(name: string, characters?: string): void {
    if (!Object.hasOwn(paletteTypes, name))
      throw new Error(`Invalid palette: ${name}`);
    const type = paletteTypes[name as Palette];
    this.patch(
      characters === undefined
        ? { palette_type: type }
        : { palette_type: type, palette_custom: characters },
    );
  }
}
