export {
  initMirrorWasm,
  cleanupMirrorWasm,
  convertFrameToAscii,
  isWasmReady,
  RenderMode,
  ColorMode,
  ColorFilter,
} from "./mirror";
export type { Palette, EmscriptenModuleFactory } from "./mirror";
export { createOptionAccessor } from "./common/optionsWrapper";
export type { WasmModule, OptionAccessor } from "./common/optionsWrapper";
export {
  initializeOptions,
  cleanupOptions,
  isOptionsInitialized,
  setWidth,
  getWidth,
  setHeight,
  getHeight,
  setDimensions,
  getDimensions,
  setColorMode,
  getColorMode,
  setColorFilter,
  getColorFilter,
  setRenderMode,
  getRenderMode,
  setPalette,
  getPalette,
  setPaletteChars,
  getPaletteChars,
  setMatrixRain,
  getMatrixRain,
  setFlipX,
  getFlipX,
  setTargetFps,
  getTargetFps,
} from "./common/options";
