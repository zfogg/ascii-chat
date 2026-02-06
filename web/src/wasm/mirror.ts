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

// Import the Emscripten-generated module factory (copied by CMake to src/wasm/dist/)
// @ts-expect-error - Generated file without types
import MirrorModuleFactory from './dist/mirror.js';

let wasmModule: MirrorModule | null = null;

/**
 * Initialize the WASM module (call once at app start)
 */
export async function initMirrorWasm(): Promise<void> {
  if (wasmModule) return;

  console.log('[WASM] Loading Mirror module...');
  const module = await MirrorModuleFactory();

  // Wait for runtime initialization if needed
  if (module.onRuntimeInitialized) {
    await new Promise<void>((resolve) => {
      module.onRuntimeInitialized = () => {
        console.log('[WASM] Runtime initialized');
        resolve();
      };
    });
  }

  wasmModule = module;
  console.log('[WASM] Mirror module loaded, HEAPU8 exists:', !!module.HEAPU8);
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
let frameCallCount = 0;

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

  if (!wasmModule.HEAPU8) {
    throw new Error('WASM module not fully initialized. HEAPU8 is undefined.');
  }

  frameCallCount++;
  if (frameCallCount % 300 === 0) {
    console.log(`[WASM] Processed ${frameCallCount} frames`);
  }

  // Allocate WASM memory for input RGBA data
  const dataPtr = wasmModule._malloc(rgbaData.length);
  if (!dataPtr) {
    throw new Error(`Failed to allocate WASM memory for RGBA data (${rgbaData.length} bytes)`);
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
  } catch (err) {
    console.error('[WASM] convertFrameToAscii error:', err);
    throw err;
  } finally {
    // Always free the input buffer
    wasmModule._free(dataPtr);
  }
}

/**
 * Check if WASM module is ready
 */
export function isWasmReady(): boolean {
  return wasmModule !== null && wasmModule.HEAPU8 !== undefined;
}
