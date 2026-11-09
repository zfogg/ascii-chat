const HANDOFF_FRAME_SIZE = 64;
const HANDOFF_PROBE_PAYLOAD_SIZE = 65536;
import { getClientModule } from "../wasm/client";
import type { JoinedSession } from "./acdsProtocol";

export const HANDOFF_PACKET = 6090;
/** Scheduling adapter. The C protocol owns wire validation; ACDS runs the shared C selector. */
export class HandoffProtocol {
  private timer: ReturnType<typeof setInterval>;
  private generation = 0;
  private sequence = 0;
  private stopped = false;
  private prepared = false;
  constructor(
    private joined: JoinedSession,
    private send: (type: number, data: Uint8Array) => void,
    private commit: (id: Uint8Array) => void,
    private progress?: (stage: string) => void,
  ) {
    this.timer = setInterval(() => this.sample(), 5000);
    this.sample();
  }
  private frame(
    kind: number,
    generation: number,
    sequence: number,
  ): Uint8Array {
    const module = getClientModule();
    if (!module) throw new Error("Client WASM module is unavailable");
    const frame = module.handoffEncode(
      kind,
      this.joined.sessionId,
      this.joined.participantId,
      generation,
      sequence,
      2,
      255,
      0,
    );
    if (!frame) throw new Error("Invalid handoff identifiers");
    return frame;
  }
  private sample(): void {
    if (this.stopped) return;
    const sequence = ++this.sequence;
    try {
      this.send(HANDOFF_PACKET, this.frame(1, 0, sequence));
      const bytes = new Uint8Array(
        HANDOFF_FRAME_SIZE + HANDOFF_PROBE_PAYLOAD_SIZE,
      );
      bytes.set(this.frame(2, 0, sequence));
      this.send(HANDOFF_PACKET, bytes);
    } catch (error) {
      console.warn("[HandoffProtocol] sample unavailable", error);
    }
  }
  receive(type: number, bytes: Uint8Array): boolean {
    if (type !== HANDOFF_PACKET) return false;
    if (
      bytes.length < HANDOFF_FRAME_SIZE ||
      !this.joined.sessionId.every((value, i) => value === bytes[4 + i])
    )
      return true;
    const module = getClientModule();
    if (!module) throw new Error("Client WASM module is unavailable");
    const action = module.handoffDecode(bytes);
    if (!action) throw new Error("Invalid handoff frame");
    if (action.generation < this.generation) return true;
    this.generation = action.generation;
    if (
      action.kind === 3 &&
      action.participant.every(
        (value, i) => value === this.joined.participantId[i],
      )
    ) {
      const result = module.browser_host_create();
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
  /** Transfer the live sampling clock while the new host engine initializes. */
  adopt(
    send: (type: number, data: Uint8Array) => void,
    commit: (id: Uint8Array) => void,
    progress?: (stage: string) => void,
  ): void {
    if (this.stopped)
      throw new Error("Cannot adopt a stopped handoff protocol");
    this.send = send;
    this.commit = commit;
    this.progress = progress;
    this.releasePreparedHost();
  }
  private releasePreparedHost(): void {
    if (this.prepared) {
      getClientModule()?.browser_host_destroy();
      this.prepared = false;
    }
  }
  close(): void {
    this.stopped = true;
    clearInterval(this.timer);
    this.releasePreparedHost();
  }
}
