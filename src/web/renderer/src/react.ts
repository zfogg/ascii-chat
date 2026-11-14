/** React renderer surface bundled with the standalone ascii-chat package. */
export {
  AsciiCanvas,
  AsciiCanvas as AsciiRenderer,
} from "@ascii-chat/shared/components/AsciiRenderer/index";
export type {
  AsciiRendererHandle,
  AsciiRendererProps,
  SessionUiEvent,
} from "@ascii-chat/shared/components/AsciiRenderer/types";
export { useAsciiRendererHandle } from "@ascii-chat/shared/components/AsciiRenderer/useAsciiRendererHandle";
export { useInitAsciiRenderer } from "@ascii-chat/shared/components/AsciiRenderer/useInitAsciiRenderer";
export { browserKeyCode } from "@ascii-chat/shared/components/AsciiRenderer/keyboard";
export { browserRasterFontSize } from "@ascii-chat/shared/components/AsciiRenderer/rasterConfig";
export { RasterWorker } from "@ascii-chat/shared/components/AsciiRenderer/RasterWorker";
export type {
  RasterRequest,
  RasterResult,
  RasterWorkerRequest,
} from "@ascii-chat/shared/components/AsciiRenderer/RasterWorker";
