/**
 * H.265/HEVC encoder using WebCodecs VideoEncoder API.
 * Encodes canvas frames to H.265 bitstream for transmission to server.
 * Requires Chrome 113+ or Edge (Firefox/Safari don't support H.265 encoding).
 */
export class H265Encoder {
  private encoder: VideoEncoder | null = null;
  private pendingChunks: Array<{
    flags: number;
    width: number;
    height: number;
    data: Uint8Array;
  }> = [];
  private width = 0;
  private height = 0;
  private frameCount = 0;
  private isOpen = false;

  static isSupported(): boolean {
    return typeof VideoEncoder !== "undefined";
  }

  async initialize(width: number, height: number, fps: number): Promise<void> {
    console.time("[H265Encoder] Total codec check time");
    this.width = width;
    this.height = height;
    this.frameCount = 0;

    const bitrate = Math.max(500_000, width * height * 2 * fps);

    // Test H.265 support
    try {
      console.time("[H265Encoder] H.265 isConfigSupported");
      const h265Support = await VideoEncoder.isConfigSupported({
        codec: "hvc1",
        width,
        height,
        bitrate,
        framerate: fps,
      });
      console.timeEnd("[H265Encoder] H.265 isConfigSupported");
      console.log("[H265Encoder] H.265 support:", h265Support.supported);

      if (!h265Support.supported) {
        console.log(
          "[H265Encoder] H.265 not supported, will use IMAGE_FRAME packets",
        );
        this.isOpen = false;
        console.timeEnd("[H265Encoder] Total codec check time");
        return;
      }
    } catch (err) {
      console.log(
        "[H265Encoder] Codec check error, will use IMAGE_FRAME packets:",
        err,
      );
      this.isOpen = false;
      console.timeEnd("[H265Encoder] Total codec check time");
      return;
    }

    console.timeEnd("[H265Encoder] Total codec check time");

    this.encoder = new VideoEncoder({
      output: (chunk: EncodedVideoChunk) => {
        const flags = chunk.type === "key" ? 0x01 : 0x00;
        const buffer = new Uint8Array(chunk.byteLength);
        chunk.copyTo(buffer);
        this.pendingChunks.push({
          flags,
          width: this.width,
          height: this.height,
          data: buffer,
        });
      },
      error: (err: DOMException) => {
        console.error("[H265Encoder] Encoding error:", err);
        this.isOpen = false; // Mark encoder as closed on error
      },
    });

    this.encoder.configure({
      codec: "hvc1", // H.265/HEVC
      width,
      height,
      bitrate,
      framerate: fps,
      hardwareAcceleration: "prefer-hardware",
    });
    this.isOpen = true;
    console.log(
      `[H265Encoder] Successfully initialized with H.265: ${width}x${height} @ ${fps} FPS`,
    );
  }

  encode(frame: VideoFrame, forceKeyframe: boolean = false): void {
    if (!this.encoder || !this.isOpen) return;

    try {
      if (forceKeyframe) {
        this.encoder.encode(frame, { keyFrame: true });
      } else {
        this.encoder.encode(frame, { keyFrame: false });
      }
      this.frameCount++;
    } catch (err) {
      console.error("[H265Encoder] Failed to encode frame:", err);
      this.isOpen = false; // Mark encoder as closed on any error
    }
  }

  drain(): Array<{
    flags: number;
    width: number;
    height: number;
    data: Uint8Array;
  }> {
    const chunks = this.pendingChunks;
    this.pendingChunks = [];
    return chunks;
  }

  destroy(): void {
    this.isOpen = false;
    if (this.encoder) {
      this.encoder.close();
      this.encoder = null;
    }
    this.pendingChunks = [];
  }
}
