/** Browser HEVC upload encoder. ACIP carries self-contained Annex B access units. */
export interface H265Chunk {
  flags: number;
  width: number;
  height: number;
  data: Uint8Array;
}

type HevcConfig = VideoEncoderConfig & { hevc: { format: "annexb" } };

export class H265Encoder {
  private encoder: VideoEncoder | null = null;
  private config: HevcConfig | null = null;
  private frameCount = 0;
  private generation = 0;
  private failure: Error | null = null;
  private resizing = false;

  constructor(private output: (chunk: H265Chunk) => void) {}

  static isSupported(): boolean {
    return (
      typeof VideoEncoder !== "undefined" && typeof VideoFrame !== "undefined"
    );
  }

  async initialize(width: number, height: number, fps: number): Promise<void> {
    this.destroy();
    const generation = this.generation;
    if (!H265Encoder.isSupported())
      throw new Error(
        "HEVC encoding is unsupported: WebCodecs VideoEncoder is unavailable. Use ?encoding=raw.",
      );
    // Main profile, 8-bit 4:2:0. Probe the exact configuration used to encode.
    const config: HevcConfig = {
      codec: "hev1.1.6.L120.B0",
      width,
      height,
      framerate: fps,
      bitrate: Math.max(500_000, width * height * 2 * fps),
      latencyMode: "realtime",
      hardwareAcceleration: "prefer-hardware",
      hevc: { format: "annexb" },
    };
    let supported = false;
    try {
      const result = await VideoEncoder.isConfigSupported(config);
      supported =
        result.supported === true &&
        (result.config as HevcConfig).hevc?.format === "annexb";
    } catch {
      /* Unsupported codec/configuration is reported below. */
    }
    if (generation !== this.generation) return;
    if (!supported)
      throw new Error(
        "HEVC encoding is unsupported by this browser/device for the requested video size and frame rate. Use ?encoding=raw.",
      );
    this.config = config;
    this.failure = null;
    this.frameCount = 0;
    let firstOutput = true;
    this.encoder = new VideoEncoder({
      output: (chunk) => {
        if (generation !== this.generation || this.failure) return;
        try {
          const data = new Uint8Array(chunk.byteLength);
          chunk.copyTo(data);
          // Unknown WebCodecs dictionary members can be silently ignored. Never
          // send length-prefixed HEVC without its out-of-band decoder config.
          if (
            !(
              data[0] === 0 &&
              data[1] === 0 &&
              (data[2] === 1 || (data[2] === 0 && data[3] === 1))
            )
          )
            throw new Error(
              "Browser did not produce HEVC Annex B video. Use ?encoding=raw.",
            );
          this.output({
            flags: (chunk.type === "key" ? 1 : 0) | (firstOutput ? 2 : 0),
            width,
            height,
            data,
          });
          firstOutput = false;
        } catch (error) {
          this.failure =
            error instanceof Error ? error : new Error(String(error));
        }
      },
      error: (error) => {
        if (generation === this.generation) this.failure = error;
      },
    });
    try {
      this.encoder.configure(config);
    } catch (error) {
      this.destroy();
      throw new Error(
        `HEVC encoder initialization failed: ${String(error)} Use ?encoding=raw.`,
        { cause: error },
      );
    }
  }

  encode(frame: VideoFrame, fps = this.config?.framerate): void {
    if (this.failure) throw this.failure;
    if (!this.encoder || !this.config || this.resizing) return;
    if (
      frame.displayWidth !== this.config.width ||
      frame.displayHeight !== this.config.height ||
      fps !== this.config.framerate
    ) {
      this.resizing = true;
      // Recreate instead of relabeling pending output with the new dimensions.
      void this.initialize(frame.displayWidth, frame.displayHeight, fps ?? 30)
        .catch((error: unknown) => {
          this.failure =
            error instanceof Error ? error : new Error(String(error));
        })
        .finally(() => {
          this.resizing = false;
        });
      return;
    }
    if (this.encoder.encodeQueueSize >= 2) return;
    this.encoder.encode(frame, { keyFrame: this.frameCount % 60 === 0 });
    this.frameCount++;
  }

  destroy(): void {
    this.generation++;
    if (this.encoder && this.encoder.state !== "closed") this.encoder.close();
    this.encoder = null;
    this.config = null;
  }
}
