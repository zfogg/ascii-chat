import { Options } from "@ascii-chat/shared/wasm/options";
import type { MainModule } from "../generated/mirror-bindings";

/** Each C/WASM instance owns its state. Observers run on the browser thread. */
export interface RuntimeOptionsModule extends Pick<
  MainModule,
  "readOptions" | "patchOptions"
> {
  browser_options_revision(): number;
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
    const revision = this.module.browser_options_revision();
    if (this.snapshot?.revision === revision) return this.snapshot;
    const decoded = new Options(this.module).read();
    this.snapshot = {
      revision: decoded.revision,
      values: Object.freeze(decoded.values),
    };
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
    const committed = new Options(this.module).patch(values, expectedRevision);
    const snapshot =
      this.snapshot?.revision === committed.revision
        ? this.snapshot
        : (this.snapshot = {
            revision: committed.revision,
            values: Object.freeze(committed.values),
          });
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
