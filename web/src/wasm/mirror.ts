// TypeScript wrapper for Mirror WASM module
// Provides type-safe interface to convert_frame_to_ascii

// Type definition for the Emscripten module
interface MirrorModuleExports {
  _convert_frame_to_ascii(
    rgba_data_ptr: number,
    src_width: number,
    src_height: number,
    dst_width: number,
    dst_height: number
  ): number;
  _free_ascii_buffer(ptr: number): void;
  _malloc(size: number): number;
  _free(ptr: number): void;
}

interface MirrorModule {
  HEAPU8: Uint8Array;
  UTF8ToString(ptr: number): string;
  _convert_frame_to_ascii: MirrorModuleExports['_convert_frame_to_ascii'];
  _free_ascii_buffer: MirrorModuleExports['_free_ascii_buffer'];
  _malloc: MirrorModuleExports['_malloc'];
  _free: MirrorModuleExports['_free'];
}

// Import the Emscripten-generated module factory
// @ts-expect-error - Generated file without types
import MirrorModuleFactory from '/wasm/mirror.js';

let wasmModule: MirrorModule | null = null;

/**
 * Initialize the WASM module (call once at app start)
 */
export async function initMirrorWasm(): Promise<void> {
  if (wasmModule) return;

  console.log('[WASM] Loading Mirror module...');
  wasmModule = await MirrorModuleFactory();
  console.log('[WASM] Mirror module loaded successfully');
}

/**
 * Convert RGBA image data to ASCII string
 *
 * @param rgbaData - RGBA pixel data from canvas (4 bytes per pixel)
 * @param srcWidth - Source image width
 * @param srcHeight - Source image height
 * @param dstWidth - Target ASCII width (characters)
 * @param dstHeight - Target ASCII height (lines)
 * @returns ASCII art string
 */
export function convertFrameToAscii(
  rgbaData: Uint8Array,
  srcWidth: number,
  srcHeight: number,
  dstWidth: number,
  dstHeight: number
): string {
  if (!wasmModule) {
    throw new Error('WASM module not initialized. Call initMirrorWasm() first.');
  }

  // Allocate WASM memory for input RGBA data
  const dataPtr = wasmModule._malloc(rgbaData.length);
  if (!dataPtr) {
    throw new Error('Failed to allocate WASM memory for RGBA data');
  }

  try {
    // Copy RGBA data to WASM memory
    wasmModule.HEAPU8.set(rgbaData, dataPtr);

    // Call WASM function
    const resultPtr = wasmModule._convert_frame_to_ascii(
      dataPtr,
      srcWidth,
      srcHeight,
      dstWidth,
      dstHeight
    );

    if (!resultPtr) {
      throw new Error('WASM convert_frame_to_ascii returned null');
    }

    // Convert C string to JavaScript string
    const asciiString = wasmModule.UTF8ToString(resultPtr);

    // Free the result buffer (allocated by WASM)
    wasmModule._free_ascii_buffer(resultPtr);

    return asciiString;
  } finally {
    // Always free the input buffer
    wasmModule._free(dataPtr);
  }
}

/**
 * Check if WASM module is ready
 */
export function isWasmReady(): boolean {
  return wasmModule !== null;
}
