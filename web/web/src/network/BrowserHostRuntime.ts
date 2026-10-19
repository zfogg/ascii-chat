import { PacketType } from "../wasm/client";

/** Keeps hosting off the UI thread and bounds raw video waiting for the worker. */
export class BrowserHostRuntime {
  private worker = new Worker(
    new URL("./browserHost.worker.ts", import.meta.url),
    {
      type: "module",
    },
  );
  private pending = new Map<number, Uint8Array>();
  private busy = new Set<number>();
  private videoBusy = new Set<number>();
  private videoPending = new Map<number, VideoFrame>();
  private nextId = 1;
  private deliveryMs = 0;
  private deliveryCount = 0;
  private closed = false;
  private rejectReady: ((error: Error) => void) | null = null;
  constructor(
    private packet: (id: number, bytes: Uint8Array) => void,
    private failure: (error: Error) => void,
  ) {}
  start(): Promise<void> {
    return new Promise((resolve, reject) => {
      this.rejectReady = reject;
      this.worker.onerror = (event) =>
        this.fail(new Error(event.message || "Host worker failed."));
      this.worker.onmessage = (event) => {
        if (this.closed) return;
        const message = event.data;
        if (message.kind === "ready") {
          this.rejectReady = null;
          resolve();
        } else if (message.kind === "error") {
          this.fail(new Error(message.error));
        } else if (message.kind === "received-video") {
          this.videoBusy.delete(message.id);
          const latest = this.videoPending.get(message.id);
          this.videoPending.delete(message.id);
          if (latest) this.receiveVideoFrame(message.id, latest);
        } else if (message.kind === "received") {
          this.busy.delete(message.id);
          const latest = this.pending.get(message.id);
          this.pending.delete(message.id);
          if (latest) this.receive(message.id, PacketType.IMAGE_FRAME, latest);
        } else if (message.kind === "packets") {
          const deliveryStart = performance.now();
          try {
            for (const item of message.packets)
              this.packet(item.id, item.bytes);
          } catch (error) {
            this.fail(
              error instanceof Error ? error : new Error(String(error)),
            );
          } finally {
            this.deliveryMs += performance.now() - deliveryStart;
            this.deliveryCount++;
            if (!this.closed)
              this.worker.postMessage({
                kind: "presented",
                sentAt: message.sentAt,
              });
          }
        } else if (message.kind === "timing") {
          console.info(
            "[BrowserHostTiming]",
            JSON.stringify({
              ...message,
              deliveryMs: this.deliveryMs,
              deliveryCount: this.deliveryCount,
            }),
          );
          this.deliveryMs = 0;
          this.deliveryCount = 0;
        }
      };
      this.worker.postMessage({ kind: "init" });
    });
  }
  addClient(local = false): number {
    const id = this.nextId++;
    this.worker.postMessage({ kind: "add", id, local });
    return id;
  }
  removeClient(id: number): void {
    this.videoPending.get(id)?.close();
    this.videoPending.delete(id);
    this.videoBusy.delete(id);
    this.pending.delete(id);
    this.busy.delete(id);
    this.worker.postMessage({ kind: "remove", id });
  }
  /** Takes ownership; only one transferred frame and one newest pending frame. */
  receiveVideoFrame(id: number, frame: VideoFrame): void {
    if (this.closed) {
      frame.close();
      return;
    }
    if (this.videoBusy.has(id)) {
      this.videoPending.get(id)?.close();
      this.videoPending.set(id, frame);
      return;
    }
    this.videoBusy.add(id);
    this.worker.postMessage({ kind: "video", id, frame }, [frame]);
  }
  receive(id: number, type: number, payload: Uint8Array): void {
    if (this.closed) return;
    if (type === PacketType.IMAGE_FRAME) {
      if (this.busy.has(id)) {
        this.pending.set(id, payload.slice());
        return;
      }
      this.busy.add(id);
    } else {
      // A stream-control packet supersedes any raw frame waiting behind it.
      this.pending.delete(id);
      this.videoPending.get(id)?.close();
      this.videoPending.delete(id);
    }
    const bytes = payload.slice();
    this.worker.postMessage({ kind: "receive", id, type, bytes }, [
      bytes.buffer,
    ]);
  }
  private fail(error: Error): void {
    this.rejectReady?.(error);
    this.rejectReady = null;
    this.failure(error);
  }
  close(): void {
    this.closed = true;
    this.rejectReady?.(new Error("Host worker closed."));
    this.rejectReady = null;
    this.worker.terminate();
    for (const frame of this.videoPending.values()) frame.close();
    this.videoPending.clear();
    this.videoBusy.clear();
    this.pending.clear();
    this.busy.clear();
  }
}
