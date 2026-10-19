interface ExposureSettings {
  exposureMode?: string | undefined;
  exposureTime?: number | undefined;
}
interface ExposureCapabilities {
  exposureMode?: string[];
  exposureTime?: { min: number; max: number; step?: number };
}
const original = new WeakMap<MediaStreamTrack, ExposureSettings>();
const operations = new WeakMap<MediaStreamTrack, Promise<void>>();

export function supportsFastExposure(track: MediaStreamTrack): boolean {
  const caps = track.getCapabilities?.() as ExposureCapabilities | undefined;
  return !!caps?.exposureMode?.includes("manual") && !!caps.exposureTime;
}

/** Exposure time uses 100 microsecond units in the camera constraints API. */
export function setFastExposure(
  track: MediaStreamTrack,
  enabled: boolean,
): Promise<void> {
  const operation = (operations.get(track) ?? Promise.resolve())
    .catch(() => {})
    .then(async () => {
      if (track.readyState !== "live") return;
      if (!enabled) {
        const capabilities = track.getCapabilities?.() as
          | ExposureCapabilities
          | undefined;
        // UVC cameras can retain manual exposure across tracks, tabs, and page
        // loads. Off must select auto even without a saved setting on this track.
        const settings = capabilities?.exposureMode?.includes("continuous")
          ? { exposureMode: "continuous" }
          : original.get(track);
        if (!settings) return;
        await track.applyConstraints({
          ...track.getConstraints(),
          advanced: [settings as MediaTrackConstraintSet],
        });
        original.delete(track);
        return;
      }
      const caps = track.getCapabilities() as ExposureCapabilities &
        MediaTrackCapabilities;
      if (!supportsFastExposure(track) || !caps.exposureTime) return;
      if (!original.has(track)) {
        const settings = track.getSettings() as MediaTrackSettings &
          ExposureSettings;
        original.set(track, {
          exposureMode: settings.exposureMode,
          exposureTime: settings.exposureTime,
        });
      }
      const fps = caps.frameRate?.max || track.getSettings().frameRate || 60;
      const range = caps.exposureTime;
      const step = range.step || 1;
      const ceiling = Math.min(range.max, 10000 / fps);
      const exposureTime = Math.max(
        range.min,
        range.min + Math.floor((ceiling - range.min) / step) * step,
      );
      let requested = exposureTime;
      for (let attempt = 0; attempt < 3; attempt++) {
        await track.applyConstraints({
          ...track.getConstraints(),
          advanced: [
            {
              exposureMode: "manual",
              exposureTime: requested,
            } as MediaTrackConstraintSet,
          ],
        });
        const actual = (track.getSettings() as ExposureSettings).exposureTime;
        if (actual === undefined || actual <= ceiling || requested <= range.min)
          break;
        // Some UVC drivers advertise linear steps but round up to powers of
        // two. Verify the applied value so rounding cannot exceed a frame.
        requested = Math.max(range.min, Math.min(requested / 2, actual / 2));
      }
    });
  operations.set(track, operation);
  return operation;
}
