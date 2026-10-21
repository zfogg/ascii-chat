/** Plaintext admission shared by WebSocket and WebRTC sessions. Encryption runs
 * only inside deliver, after capacity and final packet order are fixed. */
export type Admission =
  | "accepted"
  | "congested"
  | "dropped-stale"
  | "needs-keyframe";
export const OUTBOUND_BUDGET = 8 * 1024 * 1024;
const CONTROL_RESERVE = 512 * 1024;
const AUDIO_RESERVE = 512 * 1024;
const VIDEO_AGE_MS = 100;
const RELIABLE_WIRE_WINDOW = 256 * 1024;

type Entry = {
  packet: Uint8Array;
  type: number;
  stream: number;
  video: boolean;
  encoded: boolean;
  audio: boolean;
  keyframe: boolean;
  created: number;
  cost: number;
};

export class OutboundQueue {
  private queue: Entry[] = [];
  private bytes = 0;
  private closed = false;
  private failure: Error | null = null;
  private timer: ReturnType<typeof setTimeout> | null = null;
  private recovery = new Map<string, boolean>();
  readonly counters = {
    accepted: 0,
    submitted: 0,
    dropped: 0,
    congestion: 0,
    recoveryFrames: 0,
    peakBytes: 0,
  };

  constructor(
    private backendBytes: () => number,
    private deliver: (packet: Uint8Array) => void,
    private onError: (error: Error) => void,
    private now: () => number = () => performance.now(),
  ) {}

  needsKeyframe(type: number, stream = 0): boolean {
    return this.recovery.get(`${type}:${stream}`) !== false;
  }

  private discard(type: number, stream: number, encoded: boolean): void {
    if (encoded) this.recovery.set(`${type}:${stream}`, true);
    this.queue = this.queue.filter((entry) => {
      if (entry.video && entry.type === type && entry.stream === stream) {
        this.bytes -= entry.cost;
        this.counters.dropped++;
        return false;
      }
      return true;
    });
  }

  private expire(): void {
    const now = this.now();
    for (const entry of this.queue) {
      if (entry.video && now - entry.created > VIDEO_AGE_MS)
        this.discard(entry.type, entry.stream, entry.encoded);
    }
  }

  submit(packet: Uint8Array): Admission {
    if (this.closed) throw new Error("Outbound connection is closed");
    if (packet.length < 22) throw new Error("Missing ACIP header");
    this.expire();
    const view = new DataView(
      packet.buffer,
      packet.byteOffset,
      packet.byteLength,
    );
    const type = view.getUint16(8, false);
    const stream = view.getUint32(18, false);
    const encoded = type === 3002 || type === 3003;
    const video = encoded || type === 3001 || type === 3000;
    const audio = type === 4000 || type === 4001;
    const keyframe = encoded && !!((packet[22] ?? 0) & 1);
    const key = `${type}:${stream}`;
    if (encoded && !this.recovery.has(key)) {
      if (this.recovery.size >= 64) return "congested";
      this.recovery.set(key, true);
    }
    if (encoded && this.needsKeyframe(type, stream) && !keyframe) {
      this.counters.dropped++;
      return "needs-keyframe";
    }
    if (video && !encoded) this.discard(type, stream, false);
    // Reserve plaintext, two encryption/wire owners and descriptors together.
    const cost = 3 * (packet.length + 62) + 256;
    const limit =
      OUTBOUND_BUDGET -
      (video ? CONTROL_RESERVE + AUDIO_RESERVE : audio ? CONTROL_RESERVE : 0);
    if (
      this.queue.length >= 512 - (video ? 64 : audio ? 32 : 0) ||
      this.bytes + this.backendBytes() + cost > limit
    ) {
      if (encoded) this.discard(type, stream, true);
      this.counters.congestion++;
      return "congested";
    }
    const entry: Entry = {
      packet: packet.slice(),
      type,
      stream,
      video,
      encoded,
      audio,
      keyframe,
      cost,
      created: this.now(),
    };
    const priority = audio || type === 5001 || type === 5002;
    let afterBarrier = 0;
    if (priority)
      this.queue.forEach((queued, index) => {
        if (
          !queued.video &&
          !queued.audio &&
          queued.type !== 5001 &&
          queued.type !== 5002
        )
          afterBarrier = index + 1;
      });
    const before = priority
      ? this.queue.findIndex(
          (queued, index) => index >= afterBarrier && queued.video,
        )
      : -1;
    if (before < 0) this.queue.push(entry);
    else this.queue.splice(before, 0, entry);
    this.bytes += cost;
    if (encoded && keyframe) this.recovery.set(key, false);
    this.counters.accepted++;
    this.counters.peakBytes = Math.max(
      this.counters.peakBytes,
      this.bytes + this.backendBytes(),
    );
    this.flush();
    if (this.failure) throw this.failure;
    return "accepted";
  }

  flush(): void {
    if (this.closed) return;
    try {
      this.expire();
      while (this.queue.length) {
        const entry = this.queue[0]!;
        const age = this.now() - entry.created;
        if (!entry.video && age > (entry.audio ? 5000 : 10000))
          throw new Error(
            "Reliable outbound traffic exceeded its congestion deadline",
          );
        const buffered = this.backendBytes();
        // Background tabs may throttle the retry timer. Keep reliable traffic
        // flowing inside a bounded wire window instead of waiting for every
        // previous small packet to reach an exactly empty backend.
        if (
          buffered !== 0 &&
          (entry.video || buffered + entry.packet.length > RELIABLE_WIRE_WINDOW)
        )
          break;
        this.deliver(entry.packet);
        this.queue.shift();
        this.bytes -= entry.cost;
        this.counters.submitted++;
        if (entry.encoded && entry.keyframe) this.counters.recoveryFrames++;
      }
      if (this.queue.length && !this.timer) {
        this.timer = setTimeout(() => {
          this.timer = null;
          this.flush();
        }, 4);
      }
    } catch (error) {
      this.failure = error instanceof Error ? error : new Error(String(error));
      this.close();
      this.onError(this.failure);
    }
  }

  snapshot() {
    return {
      ...this.counters,
      applicationBytes: this.bytes,
      backendBytes: this.backendBytes(),
      oldestFrameAgeMs: Math.max(
        0,
        ...this.queue
          .filter((entry) => entry.video)
          .map((entry) => this.now() - entry.created),
      ),
    };
  }

  close(): void {
    this.closed = true;
    if (this.timer) clearTimeout(this.timer);
    this.timer = null;
    this.queue = [];
    this.bytes = 0;
    this.recovery.clear();
  }
}
