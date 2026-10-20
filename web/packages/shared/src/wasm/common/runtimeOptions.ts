/** Each C/WASM instance owns its state. Observers run on the browser thread. */
export interface RuntimeOptionsModule {
  _wasm_options_revision(): number;
  _wasm_options_retired_count?(): number;
  _wasm_options_values(): number;
  _wasm_options_patch(json: number, expectedRevision: number): number;
  _wasm_options_release(json: number): void;
  _malloc(size: number): number;
  _free(pointer: number): void;
  UTF8ToString(pointer: number): string;
  stringToUTF8(value: string, pointer: number, size: number): void;
  lengthBytesUTF8(value: string): number;
}
export type RuntimeValues = Readonly<Record<string, number | string | boolean>>;
export interface RuntimeSnapshot {
  readonly revision: number;
  readonly values: RuntimeValues;
}

export class RuntimeOptionsStore {
  private snapshot: RuntimeSnapshot | null = null;
  private notified: RuntimeSnapshot | null = null;
  private listeners = new Set<() => void>();
  private frame: number | null = null;
  constructor(readonly module: RuntimeOptionsModule) {}

  readonly getSnapshot = (): RuntimeSnapshot => {
    const revision = this.module._wasm_options_revision();
    if (this.snapshot?.revision === revision) return this.snapshot;
    const pointer = this.module._wasm_options_values();
    if (!pointer) throw new Error("Cannot read engine settings");
    try {
      const decoded = JSON.parse(
        this.module.UTF8ToString(pointer),
      ) as RuntimeSnapshot;
      this.snapshot = {
        revision: decoded.revision,
        values: Object.freeze(decoded.values),
      };
    } finally {
      this.module._wasm_options_release(pointer);
    }
    return this.snapshot;
  };

  private readonly poll = () => {
    this.frame = null;
    const snapshot = this.getSnapshot();
    if (snapshot !== this.notified) {
      this.notified = snapshot;
      for (const listener of this.listeners) listener();
    }
    if (this.listeners.size) this.frame = requestAnimationFrame(this.poll);
  };

  readonly subscribe = (listener: () => void): (() => void) => {
    if (!this.listeners.size) this.notified = this.getSnapshot();
    this.listeners.add(listener);
    if (this.frame === null) this.frame = requestAnimationFrame(this.poll);
    return () => {
      this.listeners.delete(listener);
      if (!this.listeners.size && this.frame !== null) {
        cancelAnimationFrame(this.frame);
        this.frame = null;
      }
    };
  };

  applyPatch(values: RuntimeValues, expectedRevision = -1): RuntimeSnapshot {
    const json = JSON.stringify(values);
    const size = this.module.lengthBytesUTF8(json) + 1;
    const pointer = this.module._malloc(size);
    if (!pointer) throw new Error("Cannot allocate settings patch");
    try {
      this.module.stringToUTF8(json, pointer, size);
      const result = this.module._wasm_options_patch(pointer, expectedRevision);
      if (result)
        throw new Error(
          `Settings update rejected (${result}); refresh the settings and retry.`,
        );
    } finally {
      this.module._free(pointer);
    }
    const snapshot = this.getSnapshot();
    if (this.notified !== snapshot) {
      this.notified = snapshot;
      for (const listener of this.listeners) listener();
    }
    return snapshot;
  }
}

const stores = new WeakMap<RuntimeOptionsModule, RuntimeOptionsStore>();
/** Start a new options lifetime when a retained module is initialized again. */
export function resetRuntimeOptions(module: RuntimeOptionsModule): void {
  stores.delete(module);
}
export function runtimeOptions(
  module: RuntimeOptionsModule,
): RuntimeOptionsStore {
  let store = stores.get(module);
  if (!store) {
    store = new RuntimeOptionsStore(module);
    stores.set(module, store);
  }
  return store;
}
