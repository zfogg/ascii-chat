import { getClientModule } from "../wasm/client";
import type { DiscoveryBinding } from "@ascii-chat/shared/wasm";
import { SessionRejectedError, type JoinedSession } from "./acdsProtocol";

interface Snapshot {
  state: number;
  terminal: boolean;
  error: string;
  name: string;
  joined: JoinedSession;
}
type Module = NonNullable<ReturnType<typeof getClientModule>> & {
  discoverySend?: (handle: number, type: number, payload: Uint8Array) => void;
  discoveryResult?: Snapshot;
};

/** Ownership and memory adapter for the shared C discovery state machine. */
export class DiscoveryProtocol {
  private module: Module;
  private binding: DiscoveryBinding | null;
  private outgoing: Array<[number, Uint8Array]> = [];
  private forward: NonNullable<Module["discoverySend"]>;

  constructor(private send: (type: number, payload: Uint8Array) => void) {
    const module = getClientModule() as Module | null;
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
  private finish(result: number): Snapshot {
    this.getBinding().snapshot();
    const snapshot = this.module.discoveryResult!;
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
  enter(create: boolean, name: string, password: string): Snapshot {
    const secret = new TextEncoder().encode(password);
    try {
      return this.finish(this.getBinding().enter(create, name, secret));
    } finally {
      secret.fill(0);
    }
  }
  receive(type: number, payload: Uint8Array): Snapshot {
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
      delete this.module.discoveryResult;
    }
  }
}
