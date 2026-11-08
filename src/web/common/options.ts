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
