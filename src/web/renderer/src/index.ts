import {
  Options,
  ColorMode,
  paletteName,
  defaultPalettes,
  type OptionsBindings,
  type OptionValues,
} from "@ascii-chat/shared/wasm/options";
export {
  Options,
  ColorMode,
  RenderMode,
  ColorFilter,
  TextRenderer,
  defaultPalettes,
  defaultBrowserSettings,
  colorModeValues,
  colorFilterValues,
  paletteTypes,
  paletteName,
} from "@ascii-chat/shared/wasm/options";
export type {
  OptionValue,
  OptionValues,
  OptionsSnapshot,
  Palette,
} from "@ascii-chat/shared/wasm/options";

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
  transform(
    pixels: Uint8Array | Uint8ClampedArray,
    width: number,
    height: number,
  ): number;
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
interface SessionBinding {
  valid(): boolean;
  error(): number;
  delete(): void;
}
export interface RendererModule extends OptionsBindings {
  RendererSessionBinding?: new () => SessionBinding;
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

// ANSI truecolor cells are delimited by literal ESC control characters.
// oxlint-disable-next-line no-control-regex
const TRUECOLOR_CELL = /\x1b\[38;2;(\d+);(\d+);(\d+)m([^\x1b\n])\x1b\[0m/g;

function paletteCharacter(
  red: number,
  green: number,
  blue: number,
  characters: string[],
): string {
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
function formatAudioFrame(frame: string, values: OptionValues): string {
  const colorMode = Number(values["color_mode"]);
  const palette = paletteName(Number(values["palette_type"]));
  const chars =
    palette === "custom"
      ? String(values["palette_custom"] ?? "")
      : defaultPalettes[palette];
  const characters = Array.from(chars || defaultPalettes.standard);
  return frame.replace(
    TRUECOLOR_CELL,
    (_match, redText, greenText, blueText) => {
      const red = Number(redText);
      const green = Number(greenText);
      const blue = Number(blueText);
      const character = paletteCharacter(red, green, blue, characters);

      if (colorMode === ColorMode.None) return character;
      if (colorMode === ColorMode.Ansi16) {
        return `\x1b[${ansi16Color(red, green, blue)}m${character}\x1b[0m`;
      }
      if (colorMode === ColorMode.Ansi256) {
        return `\x1b[38;5;${ansi256Color(red, green, blue)}m${character}\x1b[0m`;
      }
      return `\x1b[38;2;${red};${green};${blue}m${character}\x1b[0m`;
    },
  );
}

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
  private constructor(
    private readonly module: RendererModule,
    private readonly session?: SessionBinding,
  ) {
    this.options = new Options(module);
    this.frame = new module.MirrorFrameBinding();
  }
  static async create(init: RendererInit = {}): Promise<AsciiRenderer> {
    const { moduleUrl, ...runtime } = init;
    const path =
      moduleUrl?.toString() ?? new URL("./renderer.js", import.meta.url).href;
    const factory = (await import(/* @vite-ignore */ path)).default as (
      init: RendererInit & { getRandomValue: () => number },
    ) => Promise<RendererModule>;
    const module = await factory({
      ...runtime,
      getRandomValue: () => crypto.getRandomValues(new Uint32Array(1))[0]!,
    });
    if (!module.RendererSessionBinding)
      throw new Error("WASM module has no renderer session");
    const session = new module.RendererSessionBinding();
    if (!session.valid()) {
      const code = session.error();
      session.delete();
      throw new Error(`Failed to initialize ASCII renderer (${code})`);
    }
    try {
      return new AsciiRenderer(module, session);
    } catch (error) {
      session.delete();
      throw error;
    }
  }
  /** Use an already initialized client or mirror module without taking ownership of its session. */
  static fromModule(module: RendererModule): AsciiRenderer {
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
  transform(
    pixels: Uint8Array | Uint8ClampedArray,
    width: number,
    height: number,
  ): void {
    this.check();
    if (this.frame.transform(pixels, width, height))
      throw new Error("Capture transform failed");
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
    return this.formatAudioFrame(text);
  }
  formatAudioFrame(frame: string): string {
    this.check();
    return formatAudioFrame(frame, this.options.read().values);
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
    const matrix = values["matrix_rain"] === true;
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
    const width = raster.width(),
      height = raster.height();
    const pixels = (this.rasterPixels ??= new Uint8ClampedArray(
      width * height * 4,
    ));
    AsciiRenderer.rasterize(
      raster,
      "\x1b[H" + text,
      Number(values["zalgo"] ?? 0),
      pixels,
    );
    if (canvas.width !== width) canvas.width = width;
    if (canvas.height !== height) canvas.height = height;
    const context = canvas.getContext("2d");
    if (!context) throw new Error("Canvas 2D context unavailable");
    context.putImageData(new ImageData(pixels, width, height), 0, 0);
  }

  /** Rasterize an ANSI frame with an existing native terminal renderer. */
  static rasterize(
    raster: RasterBinding,
    text: string,
    zalgo = 0,
    pixels = new Uint8ClampedArray(raster.width() * raster.height() * 4),
  ): Uint8ClampedArray<ArrayBuffer> {
    if (
      !raster.valid() ||
      raster.setZalgo(zalgo) !== 0 ||
      raster.feed(text) !== 0
    )
      throw new Error(`Rasterization failed (${raster.error()})`);
    if (
      pixels.length !== raster.width() * raster.height() * 4 ||
      !raster.copyPixels(pixels)
    )
      throw new Error("Cannot copy raster pixels");
    return pixels;
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
      this.session?.delete();
      this.disposed = true;
    }
  }
  private check(): void {
    if (this.disposed) throw new Error("Renderer has been disposed");
  }
}
export const createRenderer = AsciiRenderer.create;
