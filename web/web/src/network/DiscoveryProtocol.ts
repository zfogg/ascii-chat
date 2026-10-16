import { getClientModule } from "../wasm/client";
import { SessionRejectedError, type JoinedSession } from "./acdsProtocol";

interface Snapshot {
  state: number;
  terminal: boolean;
  error: string;
  name: string;
  joined: JoinedSession;
}
type Module = NonNullable<ReturnType<typeof getClientModule>> & {
  _browser_discovery_new(): number;
  _browser_discovery_destroy(handle: number): void;
  _browser_discovery_enter(
    handle: number,
    create: boolean,
    name: number,
    password: number,
  ): number;
  _browser_discovery_receive(
    handle: number,
    type: number,
    data: number,
    length: number,
  ): number;
  _browser_discovery_leave(handle: number): number;
  _browser_discovery_snapshot(handle: number): void;
  discoverySend?: (handle: number, type: number, payload: Uint8Array) => void;
  discoveryResult?: Snapshot;
};

/** Ownership and memory adapter for the shared C discovery state machine. */
export class DiscoveryProtocol {
  private module: Module;
  private handle: number;
  private outgoing: Array<[number, Uint8Array]> = [];
  private forward: NonNullable<Module["discoverySend"]>;

  constructor(private send: (type: number, payload: Uint8Array) => void) {
    const module = getClientModule() as Module | null;
    if (!module) throw new Error("WASM is not initialized");
    this.module = module;
    this.handle = module._browser_discovery_new();
    if (!this.handle) throw new Error("Could not allocate discovery session");
    this.forward = (handle, type, payload) => {
      if (handle === this.handle) this.outgoing.push([type, payload]);
    };
    module.discoverySend = this.forward;
  }
  private finish(result: number): Snapshot {
    this.module._browser_discovery_snapshot(this.handle);
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
    const nameSize = new TextEncoder().encode(name).length + 1;
    const passwordSize = new TextEncoder().encode(password).length + 1;
    const namePtr = this.module._malloc(nameSize);
    const passwordPtr = this.module._malloc(passwordSize);
    try {
      if (!namePtr || !passwordPtr)
        throw new Error("Could not allocate discovery credentials");
      this.module.stringToUTF8(name, namePtr, nameSize);
      this.module.stringToUTF8(password, passwordPtr, passwordSize);
      return this.finish(
        this.module._browser_discovery_enter(
          this.handle,
          create,
          namePtr,
          passwordPtr,
        ),
      );
    } finally {
      if (passwordPtr)
        this.module.HEAPU8.fill(0, passwordPtr, passwordPtr + passwordSize);
      this.module._free(namePtr);
      this.module._free(passwordPtr);
    }
  }
  receive(type: number, payload: Uint8Array): Snapshot {
    const ptr = this.module._malloc(payload.length || 1);
    try {
      if (!ptr) throw new Error("Could not allocate discovery packet");
      this.module.HEAPU8.set(payload, ptr);
      return this.finish(
        this.module._browser_discovery_receive(
          this.handle,
          type,
          ptr,
          payload.length,
        ),
      );
    } finally {
      this.module._free(ptr);
    }
  }
  close(): void {
    if (!this.handle) return;
    try {
      this.finish(this.module._browser_discovery_leave(this.handle));
    } finally {
      this.module._browser_discovery_destroy(this.handle);
      this.handle = 0;
      if (this.module.discoverySend === this.forward)
        delete this.module.discoverySend;
      delete this.module.discoveryResult;
    }
  }
}
