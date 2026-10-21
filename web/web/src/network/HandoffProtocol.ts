import { getClientModule } from "../wasm/client";
import type { JoinedSession } from "./acdsProtocol";

export const HANDOFF_PACKET = 6090;
type Action = {
  kind: number;
  participant: Uint8Array;
  generation: number;
  sequence: number;
};
type Module = ReturnType<typeof getClientModule> & {
  _browser_handoff_encode(
    kind: number,
    session: number,
    id: number,
    generation: number,
    sequence: number,
    capabilities: number,
    natTier: number,
    port: number,
    out: number,
  ): void;
  _browser_handoff_decode(data: number, length: number): number;
  _browser_host_create(): number;
  _browser_host_destroy(): void;
  handoffResult: Action;
};

/** Scheduling adapter. The C protocol owns wire validation; ACDS runs the shared C selector. */
export class HandoffProtocol {
  private timer: ReturnType<typeof setInterval>;
  private generation = 0;
  private stopped = false;
  private prepared = false;
  constructor(
    private joined: JoinedSession,
    private send: (type: number, data: Uint8Array) => void,
    private commit: (id: Uint8Array) => void,
    private progress?: (stage: string) => void,
    private sequence = 0,
  ) {
    this.timer = setInterval(() => this.sample(), 5000);
    // Promotion retains the authenticated participant and its probe cadence.
    // An immediate extra sample can violate ACDS rate limiting.
    if (!sequence) this.sample();
  }
  private frame(
    kind: number,
    generation: number,
    sequence: number,
  ): Uint8Array {
    const module = getClientModule() as Module;
    const pointer = module._malloc(96);
    try {
      module.HEAPU8.set(this.joined.sessionId, pointer);
      module.HEAPU8.set(this.joined.participantId, pointer + 16);
      module._browser_handoff_encode(
        kind,
        pointer,
        pointer + 16,
        generation,
        sequence,
        2,
        255,
        0,
        pointer + 32,
      );
      return module.HEAPU8.slice(pointer + 32, pointer + 96);
    } finally {
      module._free(pointer);
    }
  }
  private sample(): void {
    if (this.stopped) return;
    const sequence = ++this.sequence;
    try {
      this.send(HANDOFF_PACKET, this.frame(1, 0, sequence));
      const bytes = new Uint8Array(64 + 65536);
      bytes.set(this.frame(2, 0, sequence));
      this.send(HANDOFF_PACKET, bytes);
    } catch (error) {
      console.warn("[HandoffProtocol] sample unavailable", error);
    }
  }
  receive(type: number, bytes: Uint8Array): boolean {
    if (type !== HANDOFF_PACKET) return false;
    if (
      bytes.length < 64 ||
      !this.joined.sessionId.every((value, i) => value === bytes[4 + i])
    )
      return true;
    const module = getClientModule() as Module;
    const pointer = module._malloc(bytes.length);
    let action: Action;
    try {
      module.HEAPU8.set(bytes, pointer);
      if (module._browser_handoff_decode(pointer, bytes.length))
        throw new Error("Invalid handoff frame");
      action = module.handoffResult;
    } finally {
      module._free(pointer);
    }
    if (action.generation < this.generation) return true;
    this.generation = action.generation;
    if (
      action.kind === 3 &&
      action.participant.every(
        (value, i) => value === this.joined.participantId[i],
      )
    ) {
      const result = module._browser_host_create();
      this.prepared = result === 0;
      this.send(
        HANDOFF_PACKET,
        this.frame(result ? 7 : 4, action.generation, action.sequence),
      );
    } else if (action.kind === 5) {
      this.progress?.(
        `Successor prepared: ${Array.from(action.participant.slice(0, 4), (n) => n.toString(16).padStart(2, "0")).join("")}`,
      );
    } else if (action.kind === 8) {
      throw new Error(
        "No eligible successor has fresh measurements and a prepared host.",
      );
    } else if (action.kind === 6) {
      this.commit(action.participant);
    }
    return true;
  }
  close(): number {
    this.stopped = true;
    clearInterval(this.timer);
    if (this.prepared) {
      (getClientModule() as Module | null)?._browser_host_destroy();
      this.prepared = false;
    }
    return this.sequence;
  }
}
