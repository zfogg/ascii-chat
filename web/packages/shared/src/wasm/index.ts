export { createWasmModule, getWasmAssetUrl } from "./assetUrl";
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
  | "browser_error_dismiss"
  | "network_error_action"
  | "client_cleanup"
  | "client_generate_keypair"
  | "client_get_connection_state"
  | "client_resolve_crypto_policy"
  | "client_handshake_begin"
  | "client_acds_handshake_begin"
>;

export { runtimeOptions, RuntimeOptionsStore } from "./common/runtimeOptions";
export type {
  RuntimeOptionsModule,
  RuntimeValues,
  RuntimeSnapshot,
} from "./common/runtimeOptions";
