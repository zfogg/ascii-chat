export const RECORDING_LIMIT_BYTES = 128 * 1024 * 1024;
export const RECORDING_LIMIT_MS = 10 * 60 * 1000;
const MIME_TYPES = [
  "video/webm;codecs=vp9,opus",
  "video/webm;codecs=vp8,opus",
  "video/webm",
  "video/mp4",
];

/** Return the preferred supported container/codec, or null without an encoder. */
export function recordingMimeType(): string | null {
  if (typeof MediaRecorder === "undefined") return null;
  return MIME_TYPES.find((type) => MediaRecorder.isTypeSupported(type)) ?? null;
}

export type RecordingState = "idle" | "starting" | "recording" | "finalizing";
export interface RecordingResult {
  blob: Blob;
  extension: string;
}

/** Own only browser encoding and copies of the already displayed C raster. */
export class LocalRecording {
  private recorder: MediaRecorder | null = null;
  private stream: MediaStream | null = null;
  private frame = 0;
  private deadline: ReturnType<typeof setTimeout> | undefined;
  private finalization: ReturnType<typeof setTimeout> | undefined;
  private chunks: Blob[] = [];
  private bytes = 0;
  private disposed = false;
  private failure: string | undefined;
  private removeEnded: (() => void)[] = [];
  private resolve: ((result: RecordingResult | null) => void) | undefined;
  private completion: Promise<RecordingResult | null> | undefined;

  constructor(
    private onState: (state: RecordingState) => void,
    private onError: (message: string) => void,
  ) {}

  /** Copy the displayed raster and clone bus audio; leave source tracks owned by callers. */
  start(canvas: HTMLCanvasElement, audio: MediaStream): void {
    if (this.disposed || this.recorder) throw new Error("Recording is busy");
    const mimeType = recordingMimeType();
    if (!mimeType || typeof canvas.captureStream !== "function")
      throw new Error("This browser cannot record ASCII video with audio.");
    if (!canvas.width || !canvas.height)
      throw new Error("Wait for an ASCII frame before recording.");
    // Freeze export dimensions across source changes and window resizes. This
    // copies final pixels, with no second renderer or settings/flip policy.
    const output = document.createElement("canvas");
    const scale = Math.min(1, 1920 / canvas.width, 1080 / canvas.height);
    output.width = Math.max(2, Math.floor((canvas.width * scale) / 2) * 2);
    output.height = Math.max(2, Math.floor((canvas.height * scale) / 2) * 2);
    const context = output.getContext("2d");
    if (!context)
      throw new Error("Cannot copy the ASCII canvas for recording.");
    const paint = () => {
      context.fillStyle = "black";
      context.fillRect(0, 0, output.width, output.height);
      const fit = Math.min(
        output.width / canvas.width,
        output.height / canvas.height,
      );
      const width = canvas.width * fit;
      const height = canvas.height * fit;
      context.imageSmoothingEnabled = false;
      if (width && height)
        context.drawImage(
          canvas,
          (output.width - width) / 2,
          (output.height - height) / 2,
          width,
          height,
        );
      this.frame = requestAnimationFrame(paint);
    };
    try {
      paint();
      this.stream = output.captureStream(30);
      for (const track of audio.getAudioTracks())
        this.stream.addTrack(track.clone());
      for (const track of [
        ...audio.getAudioTracks(),
        ...this.stream.getVideoTracks(),
      ]) {
        const ended = () => this.stop();
        track.addEventListener("ended", ended);
        this.removeEnded.push(() => track.removeEventListener("ended", ended));
      }
      const recorder = new MediaRecorder(this.stream, {
        mimeType,
        videoBitsPerSecond: 2_500_000,
        audioBitsPerSecond: 128_000,
      });
      this.recorder = recorder;
      this.completion = new Promise((resolve) => {
        this.resolve = resolve;
      });
      recorder.ondataavailable = ({ data }) => {
        if (this.disposed || this.failure || !data.size) return;
        if (this.bytes + data.size > RECORDING_LIMIT_BYTES) {
          this.failure =
            "Recording reached the 128 MiB memory limit. Record a shorter clip.";
          void this.stop();
          return;
        }
        this.chunks.push(data);
        this.bytes += data.size;
        // Leave headroom for the encoder's final chunk instead of losing a
        // long clip exactly at the hard memory bound.
        if (this.bytes >= RECORDING_LIMIT_BYTES - 8 * 1024 * 1024)
          void this.stop();
      };
      recorder.onerror = () => {
        this.failure = "The browser encoder failed to record this session.";
        void this.stop();
      };
      recorder.onstop = () => this.finish();
      recorder.start(1000);
      this.onState("recording");
      this.deadline = setTimeout(() => {
        void this.stop();
      }, RECORDING_LIMIT_MS);
    } catch (error) {
      this.cleanup();
      this.recorder = null;
      throw error;
    }
  }

  /** Stop once and await the final encoder chunk, bounded by the watchdog. */
  stop(): Promise<RecordingResult | null> {
    const recorder = this.recorder;
    if (!recorder) return this.completion ?? Promise.resolve(null);
    if (!this.finalization) {
      this.onState("finalizing");
      clearTimeout(this.deadline);
      cancelAnimationFrame(this.frame);
      this.finalization = setTimeout(() => {
        this.failure =
          "The browser did not finish the recording. Try a shorter clip.";
        this.finish();
      }, 10_000);
      if (recorder.state !== "inactive") {
        try {
          recorder.stop();
        } catch {
          this.failure = "The browser could not finalize this recording.";
          this.finish();
        }
      }
    }
    return this.completion ?? Promise.resolve(null);
  }

  /** Observe completion, including automatic duration, byte, or track-end stops. */
  completed(): Promise<RecordingResult | null> {
    return this.completion ?? Promise.resolve(null);
  }

  /** Settle the export once and detach encoder callbacks before releasing tracks. */
  private finish(): void {
    const recorder = this.recorder;
    if (!recorder) return;
    const type = recorder.mimeType;
    const result =
      !this.failure && !this.disposed && this.bytes
        ? {
            blob: new Blob(this.chunks, { type }),
            extension: type.includes("mp4") ? "mp4" : "webm",
          }
        : null;
    const failure = this.failure;
    this.cleanup();
    this.recorder = null;
    this.chunks = [];
    this.bytes = 0;
    this.resolve?.(result);
    this.resolve = undefined;
    if (!this.disposed) {
      if (failure) this.onError(failure);
      else if (!result)
        this.onError("The browser produced an empty recording.");
      this.onState("idle");
    }
  }

  /** Release only recorder-owned streams, callbacks, animation and timers. */
  private cleanup(): void {
    clearTimeout(this.deadline);
    clearTimeout(this.finalization);
    this.deadline = this.finalization = undefined;
    cancelAnimationFrame(this.frame);
    this.removeEnded.splice(0).forEach((remove) => remove());
    this.stream?.getTracks().forEach((track) => track.stop());
    this.stream = null;
    if (this.recorder) {
      this.recorder.onstop = null;
      this.recorder.ondataavailable = null;
      this.recorder.onerror = null;
    }
  }

  /** Cancel without a download or late UI callbacks when the session unmounts. */
  dispose(): void {
    this.disposed = true;
    const recorder = this.recorder;
    if (recorder?.state !== "inactive") {
      try {
        recorder?.stop();
      } catch {
        /* already stopped */
      }
    }
    this.finish();
    this.cleanup();
  }
}
