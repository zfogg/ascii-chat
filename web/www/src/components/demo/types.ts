import type {
  ColorMode,
  ColorFilter,
  RenderMode,
  Palette,
  TextRenderer,
} from "@ascii-chat/shared/wasm";

export interface DemoSettings {
  colorMode?: ColorMode;
  colorFilter?: ColorFilter;
  renderMode?: RenderMode;
  renderer?: TextRenderer;
  stabilize?: boolean;
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
  faceFollowDebug?: boolean;
}

export interface DemoOption {
  id: string;
  label: string;
  description?: string;
  settings: DemoSettings;
}
