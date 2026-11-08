import {
  Options,
  type OptionValues,
} from "../../../../../../src/web/common/options";
import type { RuntimeOptionsModule } from "./runtimeOptions";

/** Every browser module exposes the same atomic options interface. */
export type WasmModule = RuntimeOptionsModule & { HEAPU8: Uint8Array };
export interface OptionAccessor {
  patch(values: OptionValues): void;
  setInt(name: string, value: number): void;
  getInt(name: string): number;
  setBool(name: string, value: boolean): void;
  getBool(name: string): boolean;
  setString(name: string, value: string): void;
  getString(name: string): string;
}
const field = (name: string) =>
  name === "target_fps"
    ? "fps"
    : name === "palette"
      ? "palette_type"
      : name === "palette_chars"
        ? "palette_custom"
        : name;
export function createOptionAccessor(module: WasmModule): OptionAccessor {
  const options = new Options(module);
  const get = (name: string) => options.read().values[field(name)];
  const patch = (values: OptionValues) => {
    options.patch(values);
  };
  return {
    patch,
    setInt: (name, value) => patch({ [field(name)]: value }),
    getInt: (name) => Number(get(name)),
    setBool: (name, value) => patch({ [field(name)]: value }),
    getBool: (name) => Boolean(get(name)),
    setString: (name, value) => {
      if (name === "palette") options.palette(value);
      else if (name === "palette_chars")
        patch({ palette_custom: value, palette_type: 6 });
      else patch({ [field(name)]: value });
    },
    getString: (name) => String(get(name) ?? ""),
  };
}
