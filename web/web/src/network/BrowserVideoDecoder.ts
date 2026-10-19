import { PacketType } from "../wasm/client";
import { buildImageFramePayload } from "./packetBuilders";

/** Decode ACIP Annex B uploads; the shared C host owns composition and rendering. */
export class BrowserVideoDecoder {
  private decoder: VideoDecoder | null = null;
  private signature = "";
  private generation = 0;
  private timestamp = 0;
  private copying = false;
  private pending: {
    frame: VideoFrame;
    generation: number;
    label: string;
  } | null = null;
  constructor(
    private output: (payload: Uint8Array) => void,
    private failure: (error: Error) => void,
  ) {}
  receive(type: number, payload: Uint8Array): void {
    const label = type === PacketType.IMAGE_FRAME_H265 ? "HEVC" : "H.264";
    if (payload.length < 6) throw new Error(`Truncated ${label} frame.`);
    if (typeof VideoDecoder === "undefined")
      throw new Error(
        `${label} decoding requires WebCodecs on the host browser.`,
      );
    const view = new DataView(
      payload.buffer,
      payload.byteOffset,
      payload.byteLength,
    );
    const width = view.getUint16(1, false),
      height = view.getUint16(3, false);
    if (!width || !height || width * height > 4096 * 2160)
      throw new Error("Invalid video dimensions.");
    const signature = `${type}:${width}:${height}`;
    if (signature !== this.signature || payload[0]! & 2) {
      this.destroy();
      this.signature = signature;
      const generation = this.generation;
      this.decoder = new VideoDecoder({
        output: (frame) => {
          if (this.copying) {
            this.pending?.frame.close();
            this.pending = { frame, generation, label };
          } else this.copyFrame(frame, generation, label);
        },
        error: (error) => {
          if (generation === this.generation)
            this.failure(
              new Error(`${label} decoding failed: ${error.message}`),
            );
        },
      });
      this.decoder.configure({
        codec:
          type === PacketType.IMAGE_FRAME_H265
            ? "hev1.1.6.L120.B0"
            : "avc1.42E01F",
        codedWidth: width,
        codedHeight: height,
        optimizeForLatency: true,
      });
    }
    this.decoder!.decode(
      new EncodedVideoChunk({
        type: payload[0]! & 1 ? "key" : "delta",
        timestamp: this.timestamp++,
        data: payload.slice(5),
      }),
    );
  }
  private copyFrame(frame: VideoFrame, generation: number, label: string) {
    this.copying = true;
    const w = frame.displayWidth,
      h = frame.displayHeight;
    const rgba = new Uint8Array(w * h * 4);
    void frame
      .copyTo(rgba, { format: "RGBA", layout: [{ offset: 0, stride: w * 4 }] })
      .then(() => {
        if (generation === this.generation)
          this.output(buildImageFramePayload(rgba, w, h));
      })
      .catch((error) => {
        if (generation === this.generation)
          this.failure(
            new Error(`${label} pixel conversion failed: ${String(error)}`),
          );
      })
      .finally(() => {
        frame.close();
        this.copying = false;
        const pending = this.pending;
        this.pending = null;
        if (pending)
          this.copyFrame(pending.frame, pending.generation, pending.label);
      });
  }
  destroy() {
    this.generation++;
    this.pending?.frame.close();
    this.pending = null;
    if (this.decoder && this.decoder.state !== "closed") this.decoder.close();
    this.decoder = null;
    this.signature = "";
  }
}
