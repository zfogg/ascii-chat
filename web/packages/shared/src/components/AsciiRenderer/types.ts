export interface AsciiRendererHandle {
  writeFrame(ansiString: string): void;
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
}
