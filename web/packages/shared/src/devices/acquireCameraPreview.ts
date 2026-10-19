import { cameraRateStore, cameraAcquisitionFps } from "../capture/cameraRate";
import { acquireMediaStream } from "./acquireMediaStream";

/** Clone the live camera so device setup cannot renegotiate its capture mode. */
export async function acquireCameraPreview(
  cameraId: string,
  signal: AbortSignal,
): Promise<MediaStream> {
  signal.throwIfAborted();
  const active = cameraRateStore.activeTrack();
  if (active && (!cameraId || active.getSettings().deviceId === cameraId)) {
    return new MediaStream([active.clone()]);
  }
  return acquireMediaStream(
    {
      video: {
        width: { ideal: 1280 },
        height: { ideal: 720 },
        frameRate: { ideal: cameraAcquisitionFps() },
        ...(cameraId ? { deviceId: { exact: cameraId } } : {}),
      },
      audio: false,
    },
    signal,
  );
}
