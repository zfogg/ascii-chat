import { getClientModule } from "../wasm/client";
import type { DiscoveryBinding } from "@ascii-chat/shared/wasm";
import { SessionRejectedError, type DiscoverySnapshot } from "./acdsProtocol";

/** Ownership and memory adapter for the shared C discovery state machine. */
export class DiscoveryProtocol {
  private binding: DiscoveryBinding | null;

  constructor(private send: (type: number, payload: Uint8Array) => void) {
    const module = getClientModule();
    if (!module) throw new Error("WASM is not initialized");
    const binding = new module.DiscoveryBinding();
    if (!binding.valid()) {
      binding.delete();
      throw new Error("Could not allocate discovery session");
    }
    this.binding = binding;
  }
  private finish(result: number): DiscoverySnapshot {
    const snapshot = this.getBinding().process({
      type: "snapshot",
    }) as unknown as DiscoverySnapshot;
    if (result) {
      throw snapshot.terminal
        ? new SessionRejectedError(
            snapshot.error || "Discovery request rejected",
          )
        : new Error(snapshot.error || "Discovery request failed");
    }
    // Leave C before invoking transport callbacks, which may synchronously fail.
    const outputs = this.getBinding().drainOutputs();
    for (const output of outputs as unknown as Array<{
      type: number;
      payload: Uint8Array;
    }>)
      this.send(output.type, output.payload);
    return snapshot;
  }
  enter(create: boolean, name: string, password: string): DiscoverySnapshot {
    const secret = new TextEncoder().encode(password);
    try {
      return this.finish(
        this.getBinding().process({
          type: "enter",
          create,
          name,
          password: secret,
        })["code"] as number,
      );
    } finally {
      secret.fill(0);
    }
  }
  receive(type: number, payload: Uint8Array): DiscoverySnapshot {
    return this.finish(
      this.getBinding().process({
        type: "receive",
        packetType: type,
        packet: payload,
      })["code"] as number,
    );
  }
  private getBinding(): DiscoveryBinding {
    if (!this.binding) throw new Error("Discovery session is closed");
    return this.binding;
  }
  close(): void {
    if (!this.binding) return;
    const binding = this.binding;
    try {
      this.finish(binding.process({ type: "leave" })["code"] as number);
    } finally {
      binding.close();
      binding.delete();
      this.binding = null;
    }
  }
}
