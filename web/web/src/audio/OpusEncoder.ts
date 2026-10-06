/**
 * Opus codec wrapper for WASM
 * Provides TypeScript interface to libopus compiled to WebAssembly
 */

// Import WASM module interface (will be available after client WASM loads)
interface ClientWasmModule {
  _client_opus_encoder_init(
    sampleRate: number,
    channels: number,
    bitrate: number,
  ): number;
  _client_opus_decoder_init(sampleRate: number, channels: number): number;
  _client_opus_encode(
    pcmPtr: number,
    frameSize: number,
    opusPtr: number,
    maxBytes: number,
  ): number;
  _client_opus_decode(
    opusPtr: number,
    opusBytes: number,
    pcmPtr: number,
    frameSize: number,
    decodeFec: number,
  ): number;
  _client_opus_encoder_cleanup(): void;
  _client_opus_decoder_cleanup(): void;
  _malloc(size: number): number;
  _free(ptr: number): void;
  HEAP16: Int16Array;
  HEAPU8: Uint8Array;
}

// Get WASM module (assumes it's already loaded)
declare global {
  interface Window {
    ClientModule?: ClientWasmModule;
  }
}

export interface OpusEncoderOptions {
  sampleRate: number;
  channelCount: number;
  bitrate?: number;
}

export class OpusEncoder {
  private initialized = false;
  private wasmModule: ClientWasmModule | null = null;
  private encodePcmPtr = 0;
  private encodeOpusPtr = 0;
  private decodeOpusPtr = 0;
  private decodePcmPtr = 0;

  private static readonly MAX_OPUS_PACKET_BYTES = 65535;

  constructor(private options: OpusEncoderOptions) {}

  /**
   * Initialize Opus encoder and decoder
   */
  async init(wasmModule: ClientWasmModule): Promise<void> {
    this.wasmModule = wasmModule;

    const bitrate = this.options.bitrate || 64000; // Default 64kbps

    // Initialize encoder
    const encResult = wasmModule._client_opus_encoder_init(
      this.options.sampleRate,
      this.options.channelCount,
      bitrate,
    );

    if (encResult !== 0) {
      throw new Error("Failed to initialize Opus encoder");
    }

    // Initialize decoder
    const decResult = wasmModule._client_opus_decoder_init(
      this.options.sampleRate,
      this.options.channelCount,
    );

    if (decResult !== 0) {
      wasmModule._client_opus_encoder_cleanup();
      throw new Error("Failed to initialize Opus decoder");
    }

    // Allocate reusable codec scratch space once. Audio capture calls encode()
    // every 20 ms; allocating through the Emscripten pthread allocator from
    // that callback can re-enter a heap lock and abort the entire WASM client.
    const maxFrameSamples = Math.ceil(this.options.sampleRate * 0.12);
    const pcmBytes = maxFrameSamples * this.options.channelCount * 2;
    this.encodePcmPtr = wasmModule._malloc(pcmBytes);
    this.encodeOpusPtr = wasmModule._malloc(4000);
    this.decodeOpusPtr = wasmModule._malloc(OpusEncoder.MAX_OPUS_PACKET_BYTES);
    this.decodePcmPtr = wasmModule._malloc(pcmBytes);
    if (
      !this.encodePcmPtr ||
      !this.encodeOpusPtr ||
      !this.decodeOpusPtr ||
      !this.decodePcmPtr
    ) {
      this.freeScratchBuffers(wasmModule);
      wasmModule._client_opus_encoder_cleanup();
      wasmModule._client_opus_decoder_cleanup();
      throw new Error("Failed to allocate reusable Opus buffers");
    }

    this.initialized = true;
    console.log("[OpusEncoder] Initialized with", this.options);
  }

  /**
   * Encode PCM audio data to Opus
   * @param pcmData Int16 PCM samples
   * @returns Encoded Opus data
   */
  encode(pcmData: Int16Array): Uint8Array {
    if (!this.initialized || !this.wasmModule) {
      throw new Error("Opus encoder not initialized");
    }

    const frameSize = pcmData.length / this.options.channelCount;
    const maxOpusBytes = 4000; // Max Opus frame size
    if (frameSize > Math.ceil(this.options.sampleRate * 0.12)) {
      throw new Error("Opus input exceeds the reusable frame buffer");
    }

    // Copy PCM data to WASM memory and encode into the reusable output buffer.
    this.wasmModule.HEAP16.set(pcmData, this.encodePcmPtr >> 1);
    const encodedBytes = this.wasmModule._client_opus_encode(
      this.encodePcmPtr,
      frameSize,
      this.encodeOpusPtr,
      maxOpusBytes,
    );

    if (encodedBytes < 0) {
      throw new Error("Opus encoding failed");
    }

    const opusData = new Uint8Array(encodedBytes);
    opusData.set(
      this.wasmModule.HEAPU8.subarray(
        this.encodeOpusPtr,
        this.encodeOpusPtr + encodedBytes,
      ),
    );
    return opusData;
  }

  /**
   * Decode Opus data to PCM
   * @param opusData Encoded Opus data
   * @param frameSize Expected frame size (samples per channel)
   * @returns Decoded PCM samples
   */
  decode(opusData: Uint8Array, frameSize: number = 960): Int16Array {
    if (!this.initialized || !this.wasmModule) {
      throw new Error("Opus decoder not initialized");
    }

    if (
      frameSize > Math.ceil(this.options.sampleRate * 0.12) ||
      opusData.length > OpusEncoder.MAX_OPUS_PACKET_BYTES
    ) {
      throw new Error("Opus packet exceeds the reusable decode buffer");
    }

    this.wasmModule.HEAPU8.set(opusData, this.decodeOpusPtr);
    const decodedSamples = this.wasmModule._client_opus_decode(
      this.decodeOpusPtr,
      opusData.length,
      this.decodePcmPtr,
      frameSize,
      0, // decode_fec = false
    );

    if (decodedSamples < 0) {
      throw new Error("Opus decoding failed");
    }

    const totalSamples = decodedSamples * this.options.channelCount;
    const pcmData = new Int16Array(totalSamples);
    pcmData.set(
      this.wasmModule.HEAP16.subarray(
        this.decodePcmPtr >> 1,
        (this.decodePcmPtr >> 1) + totalSamples,
      ),
    );
    return pcmData;
  }

  private freeScratchBuffers(wasmModule: ClientWasmModule): void {
    for (const ptr of [
      this.encodePcmPtr,
      this.encodeOpusPtr,
      this.decodeOpusPtr,
      this.decodePcmPtr,
    ]) {
      if (ptr) wasmModule._free(ptr);
    }
    this.encodePcmPtr = 0;
    this.encodeOpusPtr = 0;
    this.decodeOpusPtr = 0;
    this.decodePcmPtr = 0;
  }

  /**
   * Cleanup Opus encoder and decoder
   */
  cleanup(): void {
    if (!this.initialized || !this.wasmModule) {
      return;
    }

    this.wasmModule._client_opus_encoder_cleanup();
    this.wasmModule._client_opus_decoder_cleanup();
    this.freeScratchBuffers(this.wasmModule);

    this.initialized = false;
    this.wasmModule = null;
    console.log("[OpusEncoder] Cleaned up");
  }

  /**
   * Check if encoder is ready
   */
  isReady(): boolean {
    return this.initialized;
  }
}
