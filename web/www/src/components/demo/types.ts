import type {
  ColorMode,
  ColorFilter,
  RenderMode,
  Palette,
} from "@ascii-chat/shared/wasm";

export interface DemoSettings {
  colorMode?: ColorMode;
  colorFilter?: ColorFilter;
  renderMode?: RenderMode;
  palette?: Palette;
  paletteChars?: string;
  matrixRain?: boolean;
  zalgo?: number;
  waveform?: boolean;
  fft?: boolean;
  flipX?: boolean;
  flipY?: boolean;
  targetFps?: number;
  faceFollow?: boolean;
}

export interface DemoOption {
  id: string;
  label: string;
  description?: string;
  settings: DemoSettings;
}
