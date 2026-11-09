export {
  createWasmModule,
  getWasmAssetUrl,
  getWasmCapabilities,
} from "./assetUrl";
export type { WasmCapabilityManifest } from "./assetUrl";
export type { ClientLikeKind } from "./assetUrl";
export {
  initMirrorWasm,
  adoptMirrorWasmModule,
  resetAdoptedMirrorWasmModule,
  cleanupMirrorWasm,
  cleanupRenderSession,
  resetRenderStabilizer,
  convertFrameToAscii,
  transformCaptureRgba,
  submitAudioVisualizationSamples,
  renderAudioVisualization,
  renderAudioVisualizationFrame,
  isWasmReady,
  getMirrorModule,
  RenderMode,
  ColorMode,
  ColorFilter,
} from "./mirror";
export type {
  AudioVisualizationFrameOptions,
  AudioVisualizationMode,
  AudioVisualizationSource,
  Palette,
  EmscriptenModuleFactory,
  MirrorModule,
} from "./mirror";
export {
  colorModeValues,
  colorFilterValues,
  defaultBrowserSettings,
  paletteTypes,
  paletteName,
  defaultPalettes,
  TextRenderer,
} from "@ascii-chat/shared/wasm/options";
export type { SessionUiEvent } from "../components/AsciiRenderer/types";
export type {
  AudioProcessorBinding,
  OpusCodecBinding,
  BrowserSessionBinding,
  DiscoveryBinding,
  BrowserHostBinding,
  HostPacketBinding,
  AVStateBinding,
} from "./generated/client-bindings";
export type { MainModule as ClientWasmBindings } from "./generated/client-bindings";

export { runtimeOptions, RuntimeOptionsStore } from "./common/runtimeOptions";
export type {
  RuntimeOptionsModule,
  RuntimeValues,
  RuntimeSnapshot,
} from "./common/runtimeOptions";
