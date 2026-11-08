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
/** Built-in luminance palettes, ordered from dark to bright, matching palette.h. */
export const defaultPalettes = {
  standard: "   ...',;:clodxkO0KXNWM",
  blocks: "   ░░▒▒▓▓██",
  digital: "   -=≡≣▰▱◼",
  minimal: "   .-+*#",
  cool: "   ▁▂▃▄▅▆▇█",
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
  browser_options_revision(): number;
  readOptions(): OptionsSnapshot | null;
  patchOptions(values: OptionValues, expectedRevision: number): number;
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
  return paletteNames[type] ?? "standard";
}
/** Applies a validated patch to the native options state atomically. */
export class Options {
  constructor(private readonly bindings: OptionsBindings) {}
  read(): OptionsSnapshot {
    const snapshot = this.bindings.readOptions();
    if (snapshot === null) throw new Error("Unable to read renderer options");
    return snapshot;
  }
  patch(values: OptionValues, expectedRevision = -1): OptionsSnapshot {
    const code = this.bindings.patchOptions(values, expectedRevision);
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
