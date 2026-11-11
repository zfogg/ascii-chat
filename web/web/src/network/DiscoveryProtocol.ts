import { getClientModule } from "../wasm/client";
import type { DiscoveryBinding } from "@ascii-chat/shared/wasm";
import { SessionRejectedError, type DiscoverySnapshot } from "./acdsProtocol";

/** Ownership and memory adapter for the shared C discovery state machine. */
export class DiscoveryProtocol {
  private module: NonNullable<ReturnType<typeof getClientModule>>;
  private binding: DiscoveryBinding | null;
  private outgoing: Array<[number, Uint8Array]> = [];
  private forward: NonNullable<typeof this.module.discoverySend>;

  constructor(private send: (type: number, payload: Uint8Array) => void) {
    const module = getClientModule();
    if (!module) throw new Error("WASM is not initialized");
    this.module = module;
    const binding = new module.DiscoveryBinding();
    if (!binding.valid()) {
      binding.delete();
      throw new Error("Could not allocate discovery session");
    }
    this.binding = binding;
    const token = binding.callbackToken();
    this.forward = (handle, type, payload) => {
      if (handle === token && this.binding) this.outgoing.push([type, payload]);
    };
    module.discoverySend = this.forward;
  }
  private finish(result: number): DiscoverySnapshot {
    const snapshot = this.getBinding().snapshot();
    if (result) {
      this.outgoing = [];
      throw snapshot.terminal
        ? new SessionRejectedError(
            snapshot.error || "Discovery request rejected",
          )
        : new Error(snapshot.error || "Discovery request failed");
    }
    // Leave C before invoking transport callbacks, which may synchronously fail.
    for (const [type, payload] of this.outgoing.splice(0))
      this.send(type, payload);
    return snapshot;
  }
  enter(create: boolean, name: string, password: string): DiscoverySnapshot {
    const secret = new TextEncoder().encode(password);
    try {
      return this.finish(this.getBinding().enter(create, name, secret));
    } finally {
      secret.fill(0);
    }
  }
  receive(type: number, payload: Uint8Array): DiscoverySnapshot {
    return this.finish(this.getBinding().receive(type, payload));
  }
  private getBinding(): DiscoveryBinding {
    if (!this.binding) throw new Error("Discovery session is closed");
    return this.binding;
  }
  close(): void {
    if (!this.binding) return;
    const binding = this.binding;
    try {
      this.finish(binding.leave());
    } finally {
      binding.delete();
      this.binding = null;
      if (this.module.discoverySend === this.forward)
        delete this.module.discoverySend;
    }
  }
}
