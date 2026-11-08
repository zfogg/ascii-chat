export { createWasmModule, getWasmAssetUrl } from "./assetUrl";
export type { ClientLikeKind } from "./assetUrl";
export {
  initMirrorWasm,
  adoptMirrorWasmModule,
  resetAdoptedMirrorWasmModule,
  cleanupMirrorWasm,
  convertFrameToAscii,
  transformCaptureRgba,
  submitAudioVisualizationSamples,
  submitAudioSamples,
  renderAudioVisualization,
  formatAudioVisualizationFrame,
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
  setWaveform,
  getWaveform,
  setFft,
  getFft,
  getMatrixRain,
  setFlipX,
  getFlipX,
  setFlipY,
  getFlipY,
  setTargetFps,
  getTargetFps,
} from "./common/options";

export type { SessionUiEvent } from "./sessionUi";
export type {
  AudioProcessorBinding,
  OpusCodecBinding,
  PeerCryptoBinding,
  DiscoveryBinding,
  HostPacketBinding,
  AVStateBinding,
} from "./generated/client-bindings";
export type ClientWasmBindings = Pick<
  import("./generated/client-bindings").MainModule,
  | "AudioProcessorBinding"
  | "OpusCodecBinding"
  | "PeerCryptoBinding"
  | "initClient"
  | "configureCrypto"
  | "parseKey"
  | "keyParserError"
  | "errorSnapshot"
  | "errorFrame"
  | "clientQuicInit"
  | "clientQuicTick"
  | "clientQuicEpoch"
  | "clientQuicOutput"
  | "clientQuicPacket"
  | "setServerAddress"
  | "clientPublicKey"
  | "sendClientFrame"
  | "setPalette"
  | "setPaletteChars"
  | "getPaletteChars"
  | "readOptions"
  | "handleHandshake"
  | "receiveHandshake"
  | "cryptPacket"
  | "parsePacketJson"
  | "serializePacket"
  | "receiveSettings"
  | "validateFrame"
  | "optionHelp"
  | "DiscoveryBinding"
  | "AVStateBinding"
  | "HostPacketBinding"
  | "handoffEncode"
  | "handoffDecode"
>;
export type ClientScalarBindings = Pick<
  import("./generated/client-bindings").MainModule,
  | "_wasm_error_dismiss"
  | "_network_error_action"
  | "_client_cleanup"
  | "_client_generate_keypair"
  | "_client_get_connection_state"
  | "_client_resolve_crypto_policy"
  | "_client_handshake_begin"
  | "_client_acds_handshake_begin"
  | "_set_color_mode"
  | "_get_color_mode"
  | "_set_color_filter"
  | "_get_color_filter"
  | "_set_matrix_rain"
  | "_get_matrix_rain"
  | "_set_flip_x"
  | "_get_flip_x"
  | "_set_target_fps"
  | "_get_target_fps"
  | "_set_width"
  | "_get_width"
  | "_set_height"
  | "_get_height"
>;

export { runtimeOptions, RuntimeOptionsStore } from "./common/runtimeOptions";
export type {
  RuntimeOptionsModule,
  RuntimeValues,
  RuntimeSnapshot,
} from "./common/runtimeOptions";
