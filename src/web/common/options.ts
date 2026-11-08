/** Shared Embind options interface, implemented by each browser WASM module. */
export interface OptionsBindings {
  browser_options_revision(): number;
  readOptions(): string | undefined;
  patchOptions(json: string, expectedRevision: number): number;
  setPalette?(value: string): number;
  setPaletteChars?(value: string): number;
}
export type OptionValue = number | string | boolean;
export type OptionValues = Readonly<Record<string, OptionValue>>;
export interface OptionsSnapshot {
  revision: number;
  values: OptionValues;
}

/** Applies the same validated runtime options as the native application. */
export class Options {
  constructor(private readonly bindings: OptionsBindings) {}
  read(): OptionsSnapshot {
    const json = this.bindings.readOptions();
    if (json === undefined) throw new Error("Unable to read renderer options");
    return JSON.parse(json) as OptionsSnapshot;
  }
  patch(values: OptionValues, expectedRevision = -1): OptionsSnapshot {
    const code = this.bindings.patchOptions(
      JSON.stringify(values),
      expectedRevision,
    );
    if (code !== 0)
      throw new Error(
        `Settings update rejected (${code}); refresh the settings and retry.`,
      );
    return this.read();
  }
  palette(name: string, characters?: string): void {
    if (!this.bindings.setPalette || !this.bindings.setPaletteChars)
      throw new Error("Palette settings unavailable");
    if (
      characters !== undefined &&
      this.bindings.setPaletteChars(characters) !== 0
    )
      throw new Error("Invalid custom palette");
    if (this.bindings.setPalette(name) !== 0)
      throw new Error(`Invalid palette: ${name}`);
  }
}
