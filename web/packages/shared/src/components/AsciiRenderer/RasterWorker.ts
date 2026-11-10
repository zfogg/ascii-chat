import type { ClientLikeKind } from "../../wasm/assetUrl";

export interface RasterRequest {
  text: string;
  cols: number;
  rows: number;
  matrix: boolean;
  zalgo: number;
}
export interface RasterResult {
  changed: boolean;
  pixels: Uint8ClampedArray<ArrayBuffer>;
  width: number;
  height: number;
}
export interface RasterWorkerRequest extends RasterRequest {
  clientLikeKind: ClientLikeKind;
}
/** Two rasters in flight and one replaceable newest frame; no stale-frame backlog. */
export class RasterWorker {
  private worker = new Worker(new URL("./raster.worker.ts", import.meta.url), {
    type: "module",
  });
  private inFlight = 0;
  private pending: RasterRequest | null = null;
  private closed = false;
  constructor(
    private readonly clientLikeKind: ClientLikeKind,
    present: (result: RasterResult) => void,
    failure: (error: Error) => void,
  ) {
    this.worker.onerror = (event) => failure(new Error(event.message));
    this.worker.onmessage = (event) => {
      if (this.closed) return;
      if (event.data.error) {
        failure(new Error(event.data.error));
        return;
      }
      present(event.data);
      this.inFlight--;
      const next = this.pending;
      this.pending = null;
      if (next) this.write(next);
    };
  }
  write(request: RasterRequest): void {
    if (this.closed) return;
    if (this.inFlight === 2) {
      this.pending = request;
      return;
    }
    this.inFlight++;
    this.worker.postMessage({
      ...request,
      clientLikeKind: this.clientLikeKind,
    } satisfies RasterWorkerRequest);
  }
  close(): void {
    this.closed = true;
    this.pending = null;
    this.worker.terminate();
  }
}
