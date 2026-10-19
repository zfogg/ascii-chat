/** Pace incoming snapshots without sampling them against an unrelated display clock. */
export class FramePresentationScheduler {
  private timer: ReturnType<typeof setTimeout> | null = null;
  private next = 0;
  private pending = false;
  private closed = false;
  constructor(
    private interval: () => number,
    private render: () => void,
  ) {}
  request(): void {
    if (this.closed) return;
    this.pending = true;
    if (this.timer !== null) return;
    const delay = this.next - performance.now();
    if (delay <= 1) this.flush();
    else
      this.timer = setTimeout(() => {
        this.timer = null;
        this.flush();
      }, delay);
  }
  private flush(): void {
    if (!this.pending || this.closed) return;
    this.pending = false;
    const now = performance.now();
    const interval = this.interval();
    // Preserve the cadence across small timer delays instead of accumulating
    // every scheduling delay into a progressively lower presentation rate.
    this.next += interval;
    if (this.next < now - interval) this.next = now + interval;
    this.render();
  }
  close(): void {
    this.closed = true;
    if (this.timer !== null) clearTimeout(this.timer);
    this.timer = null;
    this.pending = false;
  }
}
