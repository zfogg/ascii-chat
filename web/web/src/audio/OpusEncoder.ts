import { OPUS_FRAME_SAMPLES, OPUS_ENCODE_BUFFER_SIZE } from "./limits";
import type {
  ClientWasmBindings,
  OpusCodecBinding,
} from "@ascii-chat/shared/wasm";

export interface OpusEncoderOptions {
  sampleRate: number;
  channelCount: number;
  bitrate?: number;
}

/** Owns the module-local codec and its preallocated audio scratch buffers. */
export class OpusEncoder {
  private binding: OpusCodecBinding | null = null;
  private static readonly MAX_OPUS_PACKET_BYTES = 65535;

  constructor(private options: OpusEncoderOptions) {}

  async init(
    module: Pick<ClientWasmBindings, "OpusCodecBinding">,
  ): Promise<void> {
    if (this.binding) throw new Error("Opus encoder already initialized");
    const binding = new module.OpusCodecBinding(
      this.options.sampleRate,
      this.options.channelCount,
      this.options.bitrate || 64000,
      OPUS_ENCODE_BUFFER_SIZE,
    );
    if (!binding.valid()) {
      binding.delete();
      throw new Error("Failed to initialize Opus encoder and decoder");
    }
    this.binding = binding;
  }

  encode(pcmData: Int16Array): Uint8Array {
    if (!this.binding) throw new Error("Opus encoder not initialized");
    const frameSize = pcmData.length / this.options.channelCount;
    if (
      !Number.isInteger(frameSize) ||
      frameSize > Math.ceil(this.options.sampleRate * 0.12)
    )
      throw new Error("Opus input exceeds the reusable frame buffer");
    const result = this.binding.encode(pcmData);
    if (!result) throw new Error("Opus encoding failed");
    return result;
  }

  decode(
    opusData: Uint8Array,
    frameSize: number = OPUS_FRAME_SAMPLES,
  ): Int16Array {
    if (!this.binding) throw new Error("Opus decoder not initialized");
    if (
      !Number.isInteger(frameSize) ||
      frameSize <= 0 ||
      frameSize > Math.ceil(this.options.sampleRate * 0.12) ||
      opusData.length > OpusEncoder.MAX_OPUS_PACKET_BYTES
    )
      throw new Error("Opus packet exceeds the reusable decode buffer");
    const result = this.binding.decode(opusData, frameSize);
    if (!result) throw new Error("Opus decoding failed");
    return result;
  }

  cleanup(): void {
    this.binding?.delete();
    this.binding = null;
  }

  isReady(): boolean {
    return this.binding !== null;
  }
}
