import type { MainModule } from "./generated/mirror-bindings";
import { AsciiRenderer } from "ascii-chat";
import { resetRuntimeOptions, runtimeOptions } from "./common/runtimeOptions";
// Shared TypeScript wrapper for Mirror WASM module
// Provides type-safe interface to libasciichat mirror mode
// Decoupled from Emscripten factory location — callers provide the factory.

import {
  RenderMode,
  ColorMode,
  ColorFilter,
  type Palette,
  type OptionValues,
} from "@ascii-chat/shared/wasm/options";
export type MirrorModule = MainModule & {
  canvas?: HTMLCanvasElement;
};

// Re-export enums and types from shared options module
export { RenderMode, ColorMode, ColorFilter };
export type { Palette };

// Emscripten module factory type — callers provide this
export type EmscriptenModuleFactory = (
  moduleOverrides: Record<string, unknown>,
) => Promise<MirrorModule>;

let wasmModule: MirrorModule | null = null;
let mirrorSession: InstanceType<MirrorModule["MirrorSessionBinding"]> | null = null;
let renderer: AsciiRenderer | null = null;

function releaseFrameBinding(): void {
  renderer?.dispose();
  renderer = null;
}

/**
 * Share an already-created compatible module with the terminal renderer.
 * Discovery uses the client build, which also exports the renderer entry
 * points. Reusing it avoids starting a second Emscripten pthread runtime.
 */
export function adoptMirrorWasmModule(module: MirrorModule): void {
  if (wasmModule !== module) {
    releaseFrameBinding();
    resetRuntimeOptions(module);
  }
  wasmModule = module;
}

/** Clear a client module reference before its owning client runtime is cleaned up. */
export function resetAdoptedMirrorWasmModule(): void {
  releaseFrameBinding();
  mirrorSession?.resetRender();
  if (wasmModule) resetRuntimeOptions(wasmModule);
  wasmModule = null;
}

/**
 * Initialize the WASM module (call once at app start)
 * @param moduleFactory - Emscripten module factory function (from mirror.js)
 * @param options - Optional overrides (e.g., locateFile for cross-origin .wasm loading)
 */
export async function initMirrorWasm(
  moduleFactory: EmscriptenModuleFactory,
  options?: {
    locateFile?: (path: string) => string;
    mainScriptUrlOrBlob?: string;
    initialOptions?: OptionValues;
    pthreadPoolSize?: number;
  },
): Promise<void> {
  if (wasmModule) {
    return;
  }

  const moduleOverrides: Record<string, unknown> = {
    // libsodium crypto random - returns 32-bit unsigned integer
    getRandomValue: function () {
      const buf = new Uint32Array(1);
      crypto.getRandomValues(buf);
      return buf[0];
    },
  };

  if (options?.locateFile) {
    moduleOverrides["locateFile"] = options.locateFile;
  }
  if (options?.mainScriptUrlOrBlob) {
    moduleOverrides["mainScriptUrlOrBlob"] = options.mainScriptUrlOrBlob;
  }
  if (options?.pthreadPoolSize !== undefined) {
    moduleOverrides["pthreadPoolSize"] = options.pthreadPoolSize;
  }

  try {
    wasmModule = await moduleFactory(moduleOverrides);
  } catch (err) {
    console.error("Failed to load WASM module:", err);
    throw err;
  }

  if (!wasmModule) {
    throw new Error("Failed to load WASM module");
  }

  try {
    mirrorSession = new wasmModule.MirrorSessionBinding(options?.initialOptions ?? {});
    if (!mirrorSession.valid()) throw new Error(`Failed to initialize mirror C code: ${mirrorSession.error()}`);

    resetRuntimeOptions(wasmModule);
  } catch (err) {
    console.error("[WASM] Failed to initialize options:", err);
    mirrorSession?.delete();
    mirrorSession = null;
    wasmModule = null;
    throw err;
  }
}

/**
 * Cleanup WASM module resources
 */
export function cleanupMirrorWasm(): void {
  releaseFrameBinding();
  if (wasmModule) {
    mirrorSession?.close();
    mirrorSession?.delete();
    mirrorSession = null;
    resetRuntimeOptions(wasmModule);
    wasmModule = null;
  }
}

export function resetRenderStabilizer(): void {
  renderer?.reset();
}

/** Release per-session rendering state while keeping the shared module alive. */
export function cleanupRenderSession(): void {
  releaseFrameBinding();
  mirrorSession?.resetRender();
}

/**
 * Convert RGBA image data to ASCII string
 *
 * @param rgbaData - RGBA pixel data from canvas (4 bytes per pixel)
 * @param srcWidth - Source image width
 * @param srcHeight - Source image height
 * @returns ASCII art string
 */
export function convertFrameToAscii(
  rgbaData: Uint8Array,
  srcWidth: number,
  srcHeight: number,
  contentFit: "contain" | "cover" = "contain",
): string {
  if (!wasmModule) {
    throw new Error(
      "WASM module not initialized. Call initMirrorWasm() first.",
    );
  }

  // Validate dimensions
  if (
    !Number.isSafeInteger(srcWidth) ||
    !Number.isSafeInteger(srcHeight) ||
    srcWidth <= 0 ||
    srcHeight <= 0 ||
    srcWidth > 0x7fffffff ||
    srcHeight > 0x7fffffff
  ) {
    throw new Error(
      `Invalid image dimensions: ${srcWidth}x${srcHeight}. Width and height must be > 0.`,
    );
  }

  // Validate RGBA data size: must be exactly width * height * 4 bytes
  const expectedSize = srcWidth * srcHeight * 4;
  if (rgbaData.length !== expectedSize) {
    throw new Error(
      `RGBA data size mismatch: expected ${expectedSize} bytes (${srcWidth}x${srcHeight}*4), got ${rgbaData.length} bytes. This can cause memory access violations in WASM.`,
    );
  }

  renderer ??= AsciiRenderer.fromModule(wasmModule);
  const frame = renderer.render(
    rgbaData,
    srcWidth,
    srcHeight,
    contentFit === "contain",
  );
  if (frame === undefined) {
    throw new Error(
      `WASM mirror_convert_frame failed for ${srcWidth}x${srcHeight}`,
    );
  }
  return frame;
}

/** Apply C's capture orientation to a JavaScript-owned RGBA array in place. */
export function transformCaptureRgba(
  rgba: Uint8ClampedArray,
  width: number,
  height: number,
): void {
  if (!wasmModule) throw new Error("WASM module not initialized");
  if (!Number.isSafeInteger(width) || !Number.isSafeInteger(height))
    throw new Error("Invalid capture dimensions");
  renderer ??= AsciiRenderer.fromModule(wasmModule);
  renderer.transform(rgba, width, height);
}

export type AudioVisualizationSource = "microphone" | "media";
export type AudioVisualizationMode = "waveform" | "fft";

/**
 * Feed PCM into the active visualization source. Capture, playback, and demo
 * callers share this boundary instead of maintaining separate WASM plumbing.
 */
export function submitAudioVisualizationSamples(
  samples: Float32Array,
  source: AudioVisualizationSource = "media",
): void {
  if (!wasmModule) throw new Error("WASM module not initialized");
  renderer ??= AsciiRenderer.fromModule(wasmModule);
  renderer.submitAudio(samples, source);
}

export function renderAudioVisualization(
  width: number,
  height: number,
  source: AudioVisualizationSource = "media",
  mode?: AudioVisualizationMode,
): string {
  if (!wasmModule) throw new Error("WASM module not initialized");
  if (mode) {
    runtimeOptions(wasmModule).applyPatch({
      waveform: mode === "waveform",
      fft: mode === "fft",
    });
  }
  renderer ??= AsciiRenderer.fromModule(wasmModule);
  const frame = renderer.renderAudio(width, height, source);
  return frame;
}

export interface AudioVisualizationFrameOptions {
  source?: AudioVisualizationSource;
  mode?: AudioVisualizationMode;
}

/** Submit one PCM frame and render it with the same source and mode. */
export function renderAudioVisualizationFrame(
  samples: Float32Array,
  width: number,
  height: number,
  { source = "media", mode }: AudioVisualizationFrameOptions = {},
): string {
  submitAudioVisualizationSamples(samples, source);
  return renderAudioVisualization(width, height, source, mode);
}

/**
 * Check if WASM module is ready
 */
export function isWasmReady(): boolean {
  return wasmModule !== null;
}

/**
 * Get the WASM module instance
 */
export function getMirrorModule(): MirrorModule | null {
  return wasmModule;
}
