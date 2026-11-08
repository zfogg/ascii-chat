import type { SessionUiModule } from "./sessionUi";
import type {
  MainModule,
  MirrorFrameBinding,
} from "./generated/mirror-bindings";
import { resetRuntimeOptions } from "./common/runtimeOptions";
// Shared TypeScript wrapper for Mirror WASM module
// Provides type-safe interface to libasciichat mirror mode
// Decoupled from Emscripten factory location — callers provide the factory.

import { createOptionAccessor, type WasmModule } from "./common/optionsWrapper";
import {
  initializeOptions,
  cleanupOptions,
  getColorMode,
  getPalette,
  getPaletteChars,
  RenderMode,
  ColorMode,
  ColorFilter,
  type Palette,
} from "./common/options";

// Type for WASM exports exposed to window.asciiChatWasm
interface AsciiChatWasmExports {
  _wasmModule?: MirrorModule;
  getHelpText?(mode: number, optionName: string): string | undefined;
}

export interface MirrorModule
  extends
    WasmModule,
    SessionUiModule,
    Pick<
      MainModule,
      | "browser_capture_transform_required"
      | "mirror_cleanup"
      | "sessionRenderResetStabilizer"
      | "sessionRenderCleanup"
    > {
  canvas?: HTMLCanvasElement;
}

// Re-export enums and types from shared options module
export { RenderMode, ColorMode, ColorFilter };
export type { Palette };

// Emscripten module factory type — callers provide this
export type EmscriptenModuleFactory = (
  moduleOverrides: Record<string, unknown>,
) => Promise<MirrorModule>;

let wasmModule: MirrorModule | null = null;
let frameBinding: MirrorFrameBinding | null = null;

function releaseFrameBinding(): void {
  frameBinding?.delete();
  frameBinding = null;
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
  initializeOptions(createOptionAccessor(module));
}

/** Clear a client module reference before its owning client runtime is cleaned up. */
export function resetAdoptedMirrorWasmModule(): void {
  releaseFrameBinding();
  wasmModule?.sessionRenderCleanup();
  if (wasmModule) resetRuntimeOptions(wasmModule);
  wasmModule = null;
  cleanupOptions();
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
    initialArgs?: string[];
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
    // Initialize C options system with basic mirror mode arguments
    // This must be called before using any getter/setter functions
    // Build space-separated argument string: "mirror ...initialArgs".
    const args = ["mirror"];
    if (options?.initialArgs) {
      args.push(...options.initialArgs);
    }
    const initResult = wasmModule.initMirror(args.join(" "));
    if (initResult !== 0) {
      throw new Error(`Failed to initialize mirror C code: ${initResult}`);
    }

    // Initialize shared options module with option accessor
    // This allows React components to call setColorMode, setPalette, etc.
    const optionsAccessor = createOptionAccessor(wasmModule);
    resetRuntimeOptions(wasmModule);
    initializeOptions(optionsAccessor);

    // Expose WASM module to window for JavaScript access (e.g., tooltips)
    const globalWindow = globalThis as typeof globalThis & {
      asciiChatWasm: AsciiChatWasmExports;
    };
    globalWindow.asciiChatWasm = {
      getHelpText: wasmModule.optionHelp.bind(wasmModule),
      _wasmModule: wasmModule,
    };
  } catch (err) {
    console.error("[WASM] Failed to initialize options:", err);
    throw err;
  }
}

/**
 * Cleanup WASM module resources
 */
export function cleanupMirrorWasm(): void {
  releaseFrameBinding();
  if (wasmModule) {
    wasmModule.mirror_cleanup();
    resetRuntimeOptions(wasmModule);
    wasmModule = null;
    cleanupOptions();
  }
}

export function resetRenderStabilizer(): void {
  wasmModule?.sessionRenderResetStabilizer();
}

/** Release per-session rendering state while keeping the shared module alive. */
export function cleanupRenderSession(): void {
  releaseFrameBinding();
  wasmModule?.sessionRenderCleanup();
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

  if (!wasmModule.HEAPU8) {
    throw new Error("WASM module not fully initialized. HEAPU8 is undefined.");
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

  // Growing the WASM heap can invalidate a view supplied by a caller.
  const input =
    rgbaData.buffer === wasmModule.HEAPU8.buffer ? rgbaData.slice() : rgbaData;
  frameBinding ??= new wasmModule.MirrorFrameBinding();
  const frame = frameBinding.convert(
    input,
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
  if (rgba.buffer === wasmModule.HEAPU8.buffer)
    throw new Error("Capture input must be JavaScript-owned");
  if (!Number.isSafeInteger(width) || !Number.isSafeInteger(height))
    throw new Error("Invalid capture dimensions");
  frameBinding ??= new wasmModule.MirrorFrameBinding();
  if (frameBinding.transform(rgba, width, height))
    throw new Error("Invalid capture dimensions");
}

export type AudioVisualizationSource = "microphone" | "media";
export type AudioVisualizationMode = "waveform" | "fft";

const AUDIO_PALETTES: Record<Palette, string> = {
  standard: "   ...',;:clodxkO0KXNWM",
  blocks: "   ░░▒▒▓▓██",
  digital: "   -=≡≣▰▱◼",
  minimal: "   .-+*#",
  cool: "   ▁▂▃▄▅▆▇█",
  custom: "",
};

// ANSI truecolor cells are delimited by literal ESC control characters.
// oxlint-disable-next-line no-control-regex
const TRUECOLOR_CELL = /\x1b\[38;2;(\d+);(\d+);(\d+)m([^\x1b\n])\x1b\[0m/g;

function paletteCharacter(red: number, green: number, blue: number): string {
  const palette =
    getPalette() === "custom"
      ? getPaletteChars()
      : AUDIO_PALETTES[getPalette()];
  const characters = Array.from(palette || AUDIO_PALETTES.standard);
  const luminance = (red * 0.2126 + green * 0.7152 + blue * 0.0722) / 255;
  return (
    characters[
      Math.min(
        characters.length - 1,
        Math.round(luminance * (characters.length - 1)),
      )
    ] ?? " "
  );
}

function ansi16Color(red: number, green: number, blue: number): number {
  const bright = Math.max(red, green, blue) > 191 ? 60 : 0;
  const bits =
    (red > 127 ? 1 : 0) | (green > 127 ? 2 : 0) | (blue > 127 ? 4 : 0);
  return 30 + bits + bright;
}

function ansi256Color(red: number, green: number, blue: number): number {
  const component = (value: number) => Math.round((value / 255) * 5);
  return 16 + 36 * component(red) + 6 * component(green) + component(blue);
}

/** Apply browser color-mode and palette settings to one generated audio frame. */
export function formatAudioVisualizationFrame(frame: string): string {
  const colorMode = getColorMode();
  return frame.replace(
    TRUECOLOR_CELL,
    (_match, redText, greenText, blueText) => {
      const red = Number(redText);
      const green = Number(greenText);
      const blue = Number(blueText);
      const character = paletteCharacter(red, green, blue);

      if (colorMode === ColorMode.NONE) return character;
      if (colorMode === ColorMode.COLOR_16) {
        return `\x1b[${ansi16Color(red, green, blue)}m${character}\x1b[0m`;
      }
      if (colorMode === ColorMode.COLOR_256) {
        return `\x1b[38;5;${ansi256Color(red, green, blue)}m${character}\x1b[0m`;
      }
      return `\x1b[38;2;${red};${green};${blue}m${character}\x1b[0m`;
    },
  );
}

function audioVisualizationSourceId(source: AudioVisualizationSource): number {
  return source === "microphone" ? 0 : 1;
}

/**
 * Feed PCM into the active visualization source. Capture, playback, and demo
 * callers share this boundary instead of maintaining separate WASM plumbing.
 */
export function submitAudioVisualizationSamples(
  samples: Float32Array,
  source: AudioVisualizationSource = "media",
): void {
  if (!wasmModule) throw new Error("WASM module not initialized");
  const input =
    samples.buffer === wasmModule.HEAPU8.buffer ? samples.slice() : samples;
  frameBinding ??= new wasmModule.MirrorFrameBinding();
  frameBinding.submitAudio(input, audioVisualizationSourceId(source));
}

/** @deprecated Use submitAudioVisualizationSamples. */
export const submitAudioSamples = submitAudioVisualizationSamples;

export function renderAudioVisualization(
  width: number,
  height: number,
  source: AudioVisualizationSource = "media",
  mode?: AudioVisualizationMode,
): string {
  if (!wasmModule) throw new Error("WASM module not initialized");
  if (mode) {
    const waveform = mode === "waveform" ? 1 : 0;
    const fft = mode === "fft" ? 1 : 0;
    wasmModule.set_waveform?.(waveform);
    wasmModule.set_fft?.(fft);
  }
  frameBinding ??= new wasmModule.MirrorFrameBinding();
  const frame = frameBinding.renderAudio(
    width,
    height,
    audioVisualizationSourceId(source),
  );
  return frame === undefined ? "" : formatAudioVisualizationFrame(frame);
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
  return wasmModule !== null && wasmModule.HEAPU8 !== undefined;
}

/**
 * Get the WASM module instance
 */
export function getMirrorModule(): MirrorModule | null {
  return wasmModule;
}
