export interface CameraRate {
  label: string;
  advertisedMaximum?: number | undefined;
  configured?: number | undefined;
  observed?: number | undefined;
  limit?: number | undefined;
}

/** Estimate source cadence before any render/upload cap; ignore stalls and duplicates. */
export class CameraCadence {
  private timestamps: number[] = [];
  private lastArrival = 0;
  private nextReport = 0;
  private candidate = 0;
  private confirmations = 0;
  sample(timestamp: number, arrival: number): number | null {
    if (!Number.isFinite(timestamp) || !Number.isFinite(arrival)) return null;
    const previous = this.timestamps.at(-1);
    if (previous !== undefined && timestamp === previous) return null;
    if (
      previous !== undefined &&
      (timestamp < previous || arrival - this.lastArrival > 250)
    ) {
      this.timestamps = [];
      this.confirmations = 0;
    }
    this.lastArrival = arrival;
    this.timestamps.push(timestamp);
    while (this.timestamps.length > 1 && timestamp - this.timestamps[0]! > 3000)
      this.timestamps.shift();
    if (timestamp - this.timestamps[0]! < 2000 || arrival < this.nextReport)
      return null;
    this.nextReport = arrival + 250;
    const intervals = this.timestamps
      .slice(1)
      .map((time, index) => time - this.timestamps[index]!)
      .sort((a, b) => a - b);
    const median = intervals[intervals.length >> 1]!;
    if (
      median <= 0 ||
      intervals[Math.floor(intervals.length * 0.75)]! -
        intervals[Math.floor(intervals.length * 0.25)]! >
        median * 0.15
    )
      return null;
    const rate = Math.max(1, Math.round(1000 / median));
    if (rate !== this.candidate) {
      this.candidate = rate;
      this.confirmations = 0;
    }
    return ++this.confirmations >= 3 ? rate : null;
  }
}

let active: MediaStreamTrack | null = null;
let cadence = new CameraCadence();
let snapshot: CameraRate = { label: "" };
const listeners = new Set<() => void>();
const videoFrameCounts = new WeakMap<HTMLVideoElement, number>();
const publish = (value: CameraRate) => {
  snapshot = value;
  for (const listener of listeners) listener();
};
export const cameraRateStore = {
  snapshot: () => snapshot,
  activeTrack: () => (active?.readyState === "live" ? active : null),
  subscribe: (listener: () => void) => {
    listeners.add(listener);
    return () => {
      listeners.delete(listener);
    };
  },
};

export function reportCameraTrack(track: MediaStreamTrack): void {
  if (track === active) return;
  active = track;
  cadence = new CameraCadence();
  const advertisedMaximum = track.getCapabilities?.().frameRate?.max;
  publish({
    label: track.label,
    advertisedMaximum,
    configured: track.getSettings?.().frameRate,
  });
}

export function reportCameraFrame(timestampMilliseconds: number): void {
  const observed = cadence.sample(timestampMilliseconds, performance.now());
  if (observed === null) return;
  const limit = Math.min(observed, snapshot.advertisedMaximum || observed);
  if (snapshot.observed === observed && snapshot.limit === limit) return;
  publish({ ...snapshot, observed, limit });
}

/** Keep acquisition independent of a lower rendering limit so recovery stays observable. */
export function cameraAcquisitionFps(track?: MediaStreamTrack): number {
  return track?.getCapabilities?.().frameRate?.max || 60;
}

export function reportCameraVideoFrame(video: HTMLVideoElement): void {
  const frames = video.getVideoPlaybackQuality?.().totalVideoFrames;
  // currentTime can advance without a fresh camera frame. Without a decoded
  // frame counter, leave observed cadence unknown instead of inventing a limit.
  if (frames === undefined || frames === videoFrameCounts.get(video)) return;
  videoFrameCounts.set(video, frames);
  reportCameraFrame(performance.now());
}

export async function preferCameraMaximum(
  track?: MediaStreamTrack,
): Promise<void> {
  const maximum = track?.getCapabilities?.().frameRate?.max;
  if (
    !track ||
    !maximum ||
    !track.applyConstraints ||
    (track.getSettings?.().frameRate ?? 0) + 1 >= maximum
  )
    return;
  try {
    await track.applyConstraints({
      ...track.getConstraints?.(),
      frameRate: { ideal: maximum },
    });
  } catch (error) {
    console.warn(
      "[Camera] Unable to negotiate the highest capture rate; keeping the current camera mode",
      error,
    );
  }
}
