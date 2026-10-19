/** Select decoded frames by media timestamp without changing playback speed. */
export class VideoFrameClock {
  private previous = -Infinity;
  private deadline = -Infinity;

  accept(seconds: number, intervalMs: number): boolean {
    const timestamp = seconds * 1000;
    if (timestamp < this.previous || !Number.isFinite(this.deadline))
      this.deadline = timestamp;
    this.previous = timestamp;
    if (timestamp + 0.5 < this.deadline) return false;
    this.deadline +=
      Math.max(
        1,
        Math.floor((timestamp + 0.5 - this.deadline) / intervalMs) + 1,
      ) * intervalMs;
    return true;
  }
}
