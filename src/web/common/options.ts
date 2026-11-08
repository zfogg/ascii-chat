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
const palettes = [
  "standard",
  "standard",
  "blocks",
  "digital",
  "minimal",
  "cool",
  "custom",
] as const;
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
    const type = palettes.lastIndexOf(name as (typeof palettes)[number]);
    if (type < 0) throw new Error(`Invalid palette: ${name}`);
    this.patch(
      characters === undefined
        ? { palette_type: type }
        : { palette_type: type, palette_custom: characters },
    );
  }
}
