/** React renderer surface bundled with the standalone ascii-chat package. */
export {
  AsciiCanvas,
  AsciiCanvas as AsciiRenderer,
} from "./AsciiRenderer/index";
export type {
  AsciiRendererHandle,
  AsciiRendererProps,
  SessionUiEvent,
} from "./AsciiRenderer/types";
export { useAsciiRendererHandle } from "./AsciiRenderer/useAsciiRendererHandle";
export { useInitAsciiRenderer } from "./AsciiRenderer/useInitAsciiRenderer";
export { browserKeyCode } from "./AsciiRenderer/keyboard";
export { browserRasterFontSize } from "./AsciiRenderer/rasterConfig";
export { RasterWorker } from "./AsciiRenderer/RasterWorker";
export type {
  RasterRequest,
  RasterResult,
  RasterWorkerRequest,
} from "./AsciiRenderer/RasterWorker";
