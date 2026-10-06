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
  onDimensionsChange?: (dims: { cols: number; rows: number }) => void;
  onFpsChange?: (fps: number) => void;
  error?: string;
  showFps?: boolean;
  connectionState?: number;
  wasmModuleReady?: boolean;
  initializeOptions?: boolean;
  matrixMode?: boolean;
}
