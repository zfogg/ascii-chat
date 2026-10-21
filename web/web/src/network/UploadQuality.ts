/** Local congestion control: no receiver bandwidth estimate is implied. */
export interface UploadFeedback {
  bufferedBytes: number;
  oldestFrameAgeMs?: number;
  droppedFrames?: number;
}

export class UploadQuality {
  private network = 1;
  private compute = 1;
  private lastSample = -Infinity;
  private healthySince: number | null = null;
  private drops = 0;
  reason: "healthy" | "network" | "encoder" = "healthy";

  sample(now: number, feedback: UploadFeedback, encoderQueue = 0): void {
    const dropped = feedback.droppedFrames ?? this.drops;
    const network =
      feedback.bufferedBytes > 64 * 1024 ||
      (feedback.oldestFrameAgeMs ?? 0) > 60 ||
      dropped > this.drops;
    // Retain drops until the next decision interval, including short spikes.
    if (now - this.lastSample < 500) return;
    this.drops = dropped;
    this.lastSample = now;
    if (network || encoderQueue >= 2) {
      this.healthySince = null;
      this.reason = network ? "network" : "encoder";
      if (network) this.network = Math.max(0.04, this.network * 0.65);
      else this.compute = Math.max(0.2, this.compute * 0.8);
    } else {
      this.healthySince ??= now;
      if (now - this.healthySince >= 5000) {
        this.network = Math.min(1, this.network + 0.05);
        this.compute = Math.min(1, this.compute + 0.05);
        this.healthySince = now;
        this.reason = "healthy";
      }
    }
  }

  target(width: number, height: number, fps: number) {
    const factor = Math.min(this.network, this.compute);
    const scale =
      factor < 0.15 ? 0.25 : factor < 0.4 ? 0.5 : factor < 0.75 ? 0.75 : 1;
    const even = (value: number) => Math.max(2, Math.floor(value / 2) * 2);
    return {
      width: even(width * scale),
      height: even(height * scale),
      fps: Math.min(
        fps,
        Math.max(Math.min(5, fps), Math.floor(fps * Math.sqrt(factor))),
      ),
      bitrate: Math.max(
        32_000,
        Math.round(
          Math.min(8_000_000, Math.max(64_000, width * height * fps * 0.12)) *
            this.network,
        ),
      ),
    };
  }
}
