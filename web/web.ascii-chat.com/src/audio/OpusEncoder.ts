/**
 * Opus codec wrapper for WASM
 * Provides TypeScript interface to libopus compiled to WebAssembly
 */

// Import WASM module interface (will be available after client WASM loads)
interface ClientWasmModule {
  _client_opus_encoder_init(sampleRate: number, channels: number, bitrate: number): number;
  _client_opus_decoder_init(sampleRate: number, channels: number): number;
  _client_opus_encode(pcmPtr: number, frameSize: number, opusPtr: number, maxBytes: number): number;
  _client_opus_decode(opusPtr: number, opusBytes: number, pcmPtr: number, frameSize: number, decodeFec: number): number;
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
      bitrate
    );

    if (encResult !== 0) {
      throw new Error('Failed to initialize Opus encoder');
    }

    // Initialize decoder
    const decResult = wasmModule._client_opus_decoder_init(
      this.options.sampleRate,
      this.options.channelCount
    );

    if (decResult !== 0) {
      throw new Error('Failed to initialize Opus decoder');
    }

    this.initialized = true;
    console.log('[OpusEncoder] Initialized with', this.options);
  }

  /**
   * Encode PCM audio data to Opus
   * @param pcmData Int16 PCM samples
   * @returns Encoded Opus data
   */
  encode(pcmData: Int16Array): Uint8Array {
    if (!this.initialized || !this.wasmModule) {
      throw new Error('Opus encoder not initialized');
    }

    const frameSize = pcmData.length / this.options.channelCount;
    const maxOpusBytes = 4000; // Max Opus frame size

    // Allocate WASM memory
    const pcmPtr = this.wasmModule._malloc(pcmData.length * 2); // Int16 = 2 bytes
    const opusPtr = this.wasmModule._malloc(maxOpusBytes);

    try {
      // Copy PCM data to WASM memory
      this.wasmModule.HEAP16.set(pcmData, pcmPtr >> 1); // Shift for Int16 indexing

      // Call Opus encode
      const encodedBytes = this.wasmModule._client_opus_encode(
        pcmPtr,
        frameSize,
        opusPtr,
        maxOpusBytes
      );

      if (encodedBytes < 0) {
        throw new Error('Opus encoding failed');
      }

      // Copy encoded data from WASM memory
      const opusData = new Uint8Array(encodedBytes);
      opusData.set(this.wasmModule.HEAPU8.subarray(opusPtr, opusPtr + encodedBytes));

      return opusData;
    } finally {
      // Free WASM memory
      this.wasmModule._free(pcmPtr);
      this.wasmModule._free(opusPtr);
    }
  }

  /**
   * Decode Opus data to PCM
   * @param opusData Encoded Opus data
   * @param frameSize Expected frame size (samples per channel)
   * @returns Decoded PCM samples
   */
  decode(opusData: Uint8Array, frameSize: number = 960): Int16Array {
    if (!this.initialized || !this.wasmModule) {
      throw new Error('Opus decoder not initialized');
    }

    const maxPcmSamples = frameSize * this.options.channelCount;

    // Allocate WASM memory
    const opusPtr = this.wasmModule._malloc(opusData.length);
    const pcmPtr = this.wasmModule._malloc(maxPcmSamples * 2); // Int16 = 2 bytes

    try {
      // Copy Opus data to WASM memory
      this.wasmModule.HEAPU8.set(opusData, opusPtr);

      // Call Opus decode
      const decodedSamples = this.wasmModule._client_opus_decode(
        opusPtr,
        opusData.length,
        pcmPtr,
        frameSize,
        0 // decode_fec = false
      );

      if (decodedSamples < 0) {
        throw new Error('Opus decoding failed');
      }

      // Copy decoded PCM from WASM memory
      const totalSamples = decodedSamples * this.options.channelCount;
      const pcmData = new Int16Array(totalSamples);
      pcmData.set(this.wasmModule.HEAP16.subarray(pcmPtr >> 1, (pcmPtr >> 1) + totalSamples));

      return pcmData;
    } finally {
      // Free WASM memory
      this.wasmModule._free(opusPtr);
      this.wasmModule._free(pcmPtr);
    }
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

    this.initialized = false;
    this.wasmModule = null;
    console.log('[OpusEncoder] Cleaned up');
  }

  /**
   * Check if encoder is ready
   */
  isReady(): boolean {
    return this.initialized;
  }
}
