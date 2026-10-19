import {
  cameraRateStore,
  cameraAcquisitionFps,
} from "../../../packages/shared/src/capture/cameraRate";

/** Preview the current source without renegotiating its hardware capture mode. */
export async function acquireCameraPreview(preferences: {
  cameraId?: string;
  microphoneId?: string;
}): Promise<MediaStream> {
  const active = cameraRateStore.activeTrack();
  const reuse =
    active &&
    (!preferences.cameraId ||
      active.getSettings().deviceId === preferences.cameraId);
  const camera = reuse ? active.clone() : null;
  try {
    const stream = await navigator.mediaDevices.getUserMedia({
      video: camera
        ? false
        : {
            frameRate: { ideal: cameraAcquisitionFps() },
            ...(preferences.cameraId
              ? { deviceId: { exact: preferences.cameraId } }
              : {}),
          },
      audio: preferences.microphoneId
        ? { deviceId: { exact: preferences.microphoneId } }
        : true,
    });
    if (camera) stream.addTrack(camera);
    return stream;
  } catch (error) {
    camera?.stop();
    throw error;
  }
}
