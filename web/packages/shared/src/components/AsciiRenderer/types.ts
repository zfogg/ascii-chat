export interface AsciiRendererHandle {
  writeFrame(
    ansiString: string,
    dimensions?: { cols: number; rows: number },
  ): boolean;
  /** Whether the last successful writeFrame call changed displayed pixels. */
  getLastFrameChanged?(): boolean;
  getDimensions(): { cols: number; rows: number };
  clear(): void;
  recreateRenderer(): void;
}

export interface AsciiRendererProps {
  /** Disable interactive terminal shortcuts for embedded demos. */
  keyboardEnabled?: boolean;
  /** Allow the loading splash; embedded demos can keep their own placeholder. */
  splashEnabled?: boolean;
  uiLoading?: boolean;
  uiMessage?: string;
  onUiAction?: (event: import("../../wasm/sessionUi").SessionUiEvent) => void;
  onDimensionsChange?: (dims: { cols: number; rows: number }) => void;
  onFpsChange?: (fps: number) => void;
  error?: string;
  showFps?: boolean;
  connectionState?: number;
  wasmModuleReady?: boolean;
  initializeOptions?: boolean;
  matrixMode?: boolean;
  /** Zero disables Zalgo; 1-10 selects its intensity. */
  zalgo?: number;
  /** Requested grid dimensions; zero fits that axis to the container. */
  columns?: number;
  rows?: number;
}
