import {
  Options,
  type OptionsBindings,
  type OptionValues,
} from "../../common/options.js";
export { Options } from "../../common/options.js";
export type {
  OptionValue,
  OptionValues,
  OptionsSnapshot,
} from "../../common/options.js";

export const ColorMode = {
  Auto: -1,
  None: 0,
  Ansi16: 1,
  Ansi256: 2,
  Truecolor: 3,
} as const;
export const RenderMode = {
  Foreground: 0,
  Background: 1,
  HalfBlock: 2,
} as const;
export const ColorFilter = {
  None: 0,
  Black: 1,
  White: 2,
  Green: 3,
  Magenta: 4,
  Fuchsia: 5,
  Orange: 6,
  Teal: 7,
  Cyan: 8,
  Pink: 9,
  Red: 10,
  Yellow: 11,
  Rainbow: 12,
} as const;
export const TextRenderer = { Luminance: 0, Glyph: 1 } as const;
/** Rendering keys supported by the shared native options registry. */
export type RendererOptions = OptionValues &
  Partial<{
    width: number;
    height: number;
    fps: number;
    color_mode: number;
    color_filter: number;
    render_mode: number;
    renderer: number;
    stabilize: boolean;
    flip_x: boolean;
    flip_y: boolean;
    matrix_rain: boolean;
    zalgo: number;
    waveform: boolean;
    fft: boolean;
  }>;
export type AudioSource = "media" | "microphone";
interface FrameBinding {
  convert(
    pixels: Uint8Array | Uint8ClampedArray,
    width: number,
    height: number,
    contain: boolean,
  ): string | undefined;
  submitAudio(samples: Float32Array, source: number): void;
  renderAudio(
    width: number,
    height: number,
    source: number,
  ): string | undefined;
  delete(): void;
}
interface RasterBinding {
  valid(): boolean;
  error(): number;
  width(): number;
  height(): number;
  setZalgo(level: number): number;
  feed(text: string): number;
  copyPixels(pixels: Uint8ClampedArray): boolean;
  delete(): void;
}
interface PatternBinding {
  valid(): boolean;
  render(
    width: number,
    height: number,
    index: number,
    time: number,
    cadence: boolean,
    output: Uint8ClampedArray,
  ): boolean;
  delete(): void;
}
interface Module extends OptionsBindings {
  initMirror(args: string): number;
  mirror_cleanup(): void;
  sessionRenderResetStabilizer(): void;
  MirrorFrameBinding: new () => FrameBinding;
  TerminalRendererBinding: new (
    cols: number,
    rows: number,
    fontSize: number,
    matrix: boolean,
    selection: number,
  ) => RasterBinding;
  TestPatternBinding: new (width: number, height: number) => PatternBinding;
}
export interface RendererInit {
  locateFile?: (path: string, prefix: string) => string;
  pthreadPoolSize?: number;
  moduleUrl?: string | URL;
}
const sourceId = (source: AudioSource) => (source === "microphone" ? 0 : 1);

/** Each instance creates an independent WASM module and owns all its state. */
export class AsciiRenderer {
  readonly options: Options;
  private readonly frame: FrameBinding;
  private disposed = false;
  private raster: RasterBinding | undefined;
  private rasterKey = "";
  private rasterPixels: Uint8ClampedArray<ArrayBuffer> | undefined;
  private pattern: PatternBinding | undefined;
  private patternKey = "";
  private constructor(private readonly module: Module) {
    this.options = new Options(module);
    this.frame = new module.MirrorFrameBinding();
  }
  static async create(init: RendererInit = {}): Promise<AsciiRenderer> {
    const { moduleUrl, ...runtime } = init;
    const path =
      moduleUrl?.toString() ?? new URL("./renderer.js", import.meta.url).href;
    const factory = (await import(/* @vite-ignore */ path)).default as (
      init: RendererInit & { getRandomValue: () => number },
    ) => Promise<Module>;
    const module = await factory({
      ...runtime,
      getRandomValue: () => crypto.getRandomValues(new Uint32Array(1))[0]!,
    });
    if (module.initMirror("mirror") !== 0)
      throw new Error("Failed to initialize ASCII renderer");
    return new AsciiRenderer(module);
  }
  configure(values: RendererOptions): void {
    this.options.patch(values);
  }
  render(
    pixels: Uint8Array | Uint8ClampedArray,
    width: number,
    height: number,
    contain = false,
  ): string {
    this.check();
    const text = this.frame.convert(pixels, width, height, contain);
    if (text === undefined) throw new Error("ASCII frame conversion failed");
    return text;
  }
  submitAudio(samples: Float32Array, source: AudioSource = "media"): void {
    this.check();
    this.frame.submitAudio(samples, sourceId(source));
  }
  renderAudio(
    width: number,
    height: number,
    source: AudioSource = "media",
  ): string {
    this.check();
    const text = this.frame.renderAudio(width, height, sourceId(source));
    if (text === undefined) throw new Error("Audio visualization failed");
    return text;
  }
  reset(): void {
    this.check();
    this.module.sessionRenderResetStabilizer();
  }
  /** Rasterize an ANSI frame to the supplied canvas with the native terminal renderer. */
  draw(
    canvas: HTMLCanvasElement,
    text: string,
    columns: number,
    rows: number,
    fontSize = 12,
  ): void {
    this.check();
    const values = this.options.read().values;
    const matrix = values.matrix_rain === true;
    const key = `${columns}:${rows}:${fontSize}:${matrix}`;
    if (this.rasterKey !== key || !this.raster) {
      this.raster?.delete();
      this.raster = new this.module.TerminalRendererBinding(
        columns,
        rows,
        fontSize,
        matrix,
        0,
      );
      this.rasterKey = key;
      this.rasterPixels = undefined;
    }
    const raster = this.raster;
    if (
      !raster.valid() ||
      raster.setZalgo(Number(values.zalgo ?? 0)) !== 0 ||
      raster.feed("\x1b[H" + text) !== 0
    )
      throw new Error(`Rasterization failed (${raster.error()})`);
    const width = raster.width(),
      height = raster.height();
    const pixels = (this.rasterPixels ??= new Uint8ClampedArray(
      width * height * 4,
    ));
    if (!raster.copyPixels(pixels))
      throw new Error("Cannot copy raster pixels");
    if (canvas.width !== width) canvas.width = width;
    if (canvas.height !== height) canvas.height = height;
    const context = canvas.getContext("2d");
    if (!context) throw new Error("Canvas 2D context unavailable");
    context.putImageData(new ImageData(pixels, width, height), 0, 0);
  }

  /** Produce the project's animated RGBA test pattern. */
  testPattern(
    width: number,
    height: number,
    index: number,
    timeMs: number,
    cadence = false,
  ): Uint8ClampedArray {
    this.check();
    const key = `${width}:${height}`;
    if (this.patternKey !== key || !this.pattern) {
      this.pattern?.delete();
      this.pattern = new this.module.TestPatternBinding(width, height);
      this.patternKey = key;
    }
    const pixels = new Uint8ClampedArray(width * height * 4);
    if (
      !this.pattern.valid() ||
      !this.pattern.render(width, height, index, timeMs, cadence, pixels)
    )
      throw new Error("Test pattern failed");
    return pixels;
  }

  dispose(): void {
    if (!this.disposed) {
      this.pattern?.delete();
      this.raster?.delete();
      this.frame.delete();
      this.module.mirror_cleanup();
      this.disposed = true;
    }
  }
  private check(): void {
    if (this.disposed) throw new Error("Renderer has been disposed");
  }
}
export const createRenderer = AsciiRenderer.create;
