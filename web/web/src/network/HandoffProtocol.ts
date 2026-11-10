import { getClientModule } from "../wasm/client";
import type { BrowserHostBinding } from "@ascii-chat/shared/wasm";
import type { JoinedSession } from "./acdsProtocol";

export const HANDOFF_PACKET = 6090;
/** Scheduling adapter. The C protocol owns wire validation; ACDS runs the shared C selector. */
export class HandoffProtocol {
  private timer: ReturnType<typeof setInterval>;
  private stopped = false;
  private prepared = false;
  private preparedHost: BrowserHostBinding | null = null;
  private binding: InstanceType<
    NonNullable<ReturnType<typeof getClientModule>>["HandoffBinding"]
  > | null = null;
  constructor(
    private joined: JoinedSession,
    private send: (type: number, data: Uint8Array) => void,
    private commit: (id: Uint8Array) => void,
    private progress?: (stage: string) => void,
  ) {
    const module = getClientModule();
    if (!module) throw new Error("Client WASM module is unavailable");
    this.binding = new module.HandoffBinding(
      this.joined.sessionId,
      this.joined.participantId,
    );
    if (!this.binding.valid()) {
      const code = this.binding.error();
      this.binding.delete();
      this.binding = null;
      throw new Error(`Handoff session initialization failed (${code})`);
    }
    this.timer = setInterval(() => this.sample(), 5000);
    this.sample();
  }
  private sample(): void {
    if (this.stopped) return;
    try {
      const result = this.binding?.process({ type: "sample" }) as {
        code: number;
        outputs?: Uint8Array[];
      };
      if (result.code)
        throw new Error(`Handoff sampling failed (${result.code})`);
      for (const packet of result.outputs ?? [])
        this.send(HANDOFF_PACKET, packet);
    } catch (error) {
      console.warn("[HandoffProtocol] sample unavailable", error);
    }
  }
  receive(type: number, bytes: Uint8Array): boolean {
    if (type !== HANDOFF_PACKET) return false;
    const actionResult = this.binding?.process({
      type: "receive",
      packet: bytes,
    }) as {
      code: number;
      handled?: boolean;
      action?: {
        kind: number;
        participant: Uint8Array;
        generation: number;
        sequence: number;
      } | null;
    };
    if (!actionResult) throw new Error("Handoff session is unavailable");
    if (actionResult.code)
      throw new Error(`Invalid handoff frame (${actionResult.code})`);
    const action = actionResult.action ?? null;
    // Native handoff state owns frame validation, session matching, and
    // generation ordering. A valid stale/irrelevant frame is handled with no
    // browser action required.
    if (!action) return true;
    if (
      action.kind === 3 &&
      action.participant.every(
        (value, i) => value === this.joined.participantId[i],
      )
    ) {
      const module = getClientModule();
      if (!module) throw new Error("Client WASM module is unavailable");
      const host = new module.BrowserHostBinding();
      this.preparedHost = host;
      this.prepared = host.valid();
      this.send(
        HANDOFF_PACKET,
        (
          this.binding?.process({
            type: "ack",
            kind: 4,
            generation: action.generation,
            sequence: action.sequence,
          }) as { bytes?: Uint8Array }
        ).bytes ?? bytes,
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
      this.preparedHost?.close();
      this.preparedHost?.delete();
      this.preparedHost = null;
      this.prepared = false;
    }
  }
  close(): void {
    this.stopped = true;
    clearInterval(this.timer);
    this.releasePreparedHost();
    this.binding?.close();
    this.binding?.delete();
    this.binding = null;
  }
}
