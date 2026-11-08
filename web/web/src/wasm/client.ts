// TypeScript wrapper for Client WASM module
// Provides type-safe interface to libasciichat client mode

import type { Palette } from "../components";
import type {
  RuntimeOptionsModule,
  RuntimeValues,
  ClientWasmBindings,
  ClientScalarBindings,
  PeerCryptoBinding,
} from "@ascii-chat/shared/wasm";
import { createWasmModule } from "@ascii-chat/shared/wasm";
import {
  publishBrowserError,
  type BrowserErrorFrame,
} from "../errors/errorBus";

export const BINARY_OPENPGP_KEY_PREFIX = "ascii-chat:openpgp-binary-base64:";

/** Preserve binary OpenPGP packet files in browser storage without text decoding. */
export async function readCryptoKeyFile(file: File): Promise<string> {
  const bytes = new Uint8Array(await file.arrayBuffer());
  let text: string | undefined;
  try {
    text = new TextDecoder("utf-8", { fatal: true }).decode(bytes);
  } catch {
    // Binary OpenPGP packets are not UTF-8 text.
  }
  if (
    text &&
    (text.includes("-----BEGIN PGP ") ||
      text.startsWith("ssh-ed25519 ") ||
      text.includes("-----BEGIN OPENSSH PRIVATE KEY-----"))
  ) {
    return text;
  }

  let binary = "";
  const chunkSize = 0x8000;
  for (let offset = 0; offset < bytes.length; offset += chunkSize) {
    binary += String.fromCharCode(
      ...bytes.subarray(offset, offset + chunkSize),
    );
  }
  return `${BINARY_OPENPGP_KEY_PREFIX}${btoa(binary)}`;
}

// Type for WASM exports exposed to window.asciiChatWasm
interface AsciiChatWasmExports {
  _wasmModule?: ClientModule;
  getHelpText?(mode: number, optionName: string): string | undefined;
}

interface ClientModule
  extends RuntimeOptionsModule, ClientWasmBindings, ClientScalarBindings {
  HEAPU8: Uint8Array;
  sendPacketCallback?: (rawPacket: Uint8Array) => void;
}
/** Publish the complete pending C error stack and throw at the JS boundary. */
export function throwWasmError(result: number, fallback: string): never {
  const module = wasmModule;
  const frames: BrowserErrorFrame[] = [];
  if (module) {
    const total = module.errorSnapshot();
    const count = Math.min(total, 64);
    for (let index = 0; index < count; index++) {
      const frame = module.errorFrame(index);
      frames.push({ ...frame, message: frame.message || fallback });
    }
  }
  publishBrowserError({
    source: "C/WASM",
    title: `${fallback} (${result})`,
    frames: frames.length ? frames : [{ code: result, message: fallback }],
    dismiss: () => module?._wasm_error_dismiss(),
  });
  const error = new Error(`${fallback} (${result})`);
  Object.defineProperty(error, "asciichatWasmReported", { value: true });
  throw error;
}

export function isReportedWasmError(error: unknown): boolean {
  return Boolean(
    error && typeof error === "object" && "asciichatWasmReported" in error,
  );
}

export interface ClientCryptoOptions {
  allowedClientPublicKeyText?: string;
  password?: string;
  identityPrivateKeyText?: string;
  expectedServerPublicKeyText?: string;
}

/** Commit the peer's effective settings in C before notifying browser observers. */
export function acceptRemoteRenderSettings(payload: Uint8Array): RuntimeValues {
  if (!wasmModule) throw new Error("Client engine is unavailable");
  const result = wasmModule.receiveSettings(payload);
  if (result.code) throwWasmError(result.code, "Invalid remote settings");
  if (result.json === null)
    throw new Error("Cannot read effective remote settings");
  return JSON.parse(result.json) as RuntimeValues;
}

// Enums matching libasciichat definitions
export enum ConnectionState {
  DISCONNECTED = 0,
  CONNECTING = 1,
  HANDSHAKE = 2,
  CONNECTED = 3,
  ERROR = 4,
}

export enum ColorMode {
  AUTO = -1,
  NONE = 0,
  COLOR_16 = 1,
  COLOR_256 = 2,
  TRUECOLOR = 3,
}

export enum ColorFilter {
  NONE = 0,
  BLACK = 1,
  WHITE = 2,
  GREEN = 3,
  MAGENTA = 4,
  FUCHSIA = 5,
  ORANGE = 6,
  TEAL = 7,
  CYAN = 8,
  PINK = 9,
  RED = 10,
  YELLOW = 11,
  RAINBOW = 12,
}

export enum PacketType {
  // Protocol negotiation
  PROTOCOL_VERSION = 1,

  // Crypto handshake (unencrypted)
  CRYPTO_CLIENT_HELLO = 1000,
  CRYPTO_CAPABILITIES = 1100,
  CRYPTO_PARAMETERS = 1101,
  CRYPTO_KEY_EXCHANGE_INIT = 1102,
  CRYPTO_KEY_EXCHANGE_RESP = 1103,
  CRYPTO_AUTH_CHALLENGE = 1104,
  CRYPTO_AUTH_RESPONSE = 1105,
  CRYPTO_AUTH_FAILED = 1106,
  CRYPTO_SERVER_AUTH_RESP = 1107,
  CRYPTO_HANDSHAKE_COMPLETE = 1108,
  CRYPTO_NO_ENCRYPTION = 1109,

  // Encrypted session data
  ENCRYPTED = 1200,

  // Rekeying
  CRYPTO_REKEY_REQUEST = 1201,
  CRYPTO_REKEY_RESPONSE = 1202,
  CRYPTO_REKEY_COMPLETE = 1203,

  // Messages
  SIZE_MESSAGE = 2000,
  AUDIO_MESSAGE = 2001,
  TEXT_MESSAGE = 2002,
  ERROR_MESSAGE = 2003,
  REMOTE_LOG = 2004,

  // Video
  ASCII_FRAME = 3000,
  IMAGE_FRAME = 3001,
  IMAGE_FRAME_H264 = 3003, // H.264/AVC encoded video frames
  IMAGE_FRAME_H265 = 3002, // H.265/HEVC encoded video frames

  // Audio
  AUDIO_BATCH = 4000,
  AUDIO_OPUS_BATCH = 4001,

  // Control
  CLIENT_CAPABILITIES = 5000,
  PING = 5001,
  PONG = 5002,
  CLIENT_JOIN = 5003,
  CLIENT_LEAVE = 5004,
  STREAM_START = 5005,
  STREAM_STOP = 5006,
  CLEAR_CONSOLE = 5007,
  SERVER_STATE = 5008,
}

/**
 * Get human-readable name for a packet type number.
 * Works for both known and unknown packet types.
 */
export function packetTypeName(type: number): string {
  const names: Record<number, string> = {
    [PacketType.PROTOCOL_VERSION]: "PROTOCOL_VERSION",
    [PacketType.CRYPTO_CLIENT_HELLO]: "CRYPTO_CLIENT_HELLO",
    [PacketType.CRYPTO_CAPABILITIES]: "CRYPTO_CAPABILITIES",
    [PacketType.CRYPTO_PARAMETERS]: "CRYPTO_PARAMETERS",
    [PacketType.CRYPTO_KEY_EXCHANGE_INIT]: "CRYPTO_KEY_EXCHANGE_INIT",
    [PacketType.CRYPTO_KEY_EXCHANGE_RESP]: "CRYPTO_KEY_EXCHANGE_RESP",
    [PacketType.CRYPTO_AUTH_CHALLENGE]: "CRYPTO_AUTH_CHALLENGE",
    [PacketType.CRYPTO_AUTH_RESPONSE]: "CRYPTO_AUTH_RESPONSE",
    [PacketType.CRYPTO_AUTH_FAILED]: "CRYPTO_AUTH_FAILED",
    [PacketType.CRYPTO_SERVER_AUTH_RESP]: "CRYPTO_SERVER_AUTH_RESP",
    [PacketType.CRYPTO_HANDSHAKE_COMPLETE]: "CRYPTO_HANDSHAKE_COMPLETE",
    [PacketType.CRYPTO_NO_ENCRYPTION]: "CRYPTO_NO_ENCRYPTION",
    [PacketType.ENCRYPTED]: "ENCRYPTED",
    [PacketType.CRYPTO_REKEY_REQUEST]: "CRYPTO_REKEY_REQUEST",
    [PacketType.CRYPTO_REKEY_RESPONSE]: "CRYPTO_REKEY_RESPONSE",
    [PacketType.CRYPTO_REKEY_COMPLETE]: "CRYPTO_REKEY_COMPLETE",
    [PacketType.SIZE_MESSAGE]: "SIZE_MESSAGE",
    [PacketType.AUDIO_MESSAGE]: "AUDIO_MESSAGE",
    [PacketType.TEXT_MESSAGE]: "TEXT_MESSAGE",
    [PacketType.ERROR_MESSAGE]: "ERROR_MESSAGE",
    [PacketType.REMOTE_LOG]: "REMOTE_LOG",
    [PacketType.ASCII_FRAME]: "ASCII_FRAME",
    [PacketType.IMAGE_FRAME]: "IMAGE_FRAME",
    [PacketType.IMAGE_FRAME_H265]: "IMAGE_FRAME_H265",
    [PacketType.IMAGE_FRAME_H264]: "IMAGE_FRAME_H264",
    [PacketType.AUDIO_BATCH]: "AUDIO_BATCH",
    [PacketType.AUDIO_OPUS_BATCH]: "AUDIO_OPUS_BATCH",
    [PacketType.CLIENT_CAPABILITIES]: "CLIENT_CAPABILITIES",
    [PacketType.PING]: "PING",
    [PacketType.PONG]: "PONG",
    [PacketType.CLIENT_JOIN]: "CLIENT_JOIN",
    [PacketType.CLIENT_LEAVE]: "CLIENT_LEAVE",
    [PacketType.STREAM_START]: "STREAM_START",
    [PacketType.STREAM_STOP]: "STREAM_STOP",
    [PacketType.CLEAR_CONSOLE]: "CLEAR_CONSOLE",
    [PacketType.SERVER_STATE]: "SERVER_STATE",
  };
  return names[type] || `UNKNOWN(${type})`;
}

let wasmModule: ClientModule | null = null;
let moduleLoading: Promise<void> | null = null;
let keyParserWorker: Worker | null = null;
let nextKeyParserRequestId = 1;
const pendingKeyParserRequests = new Map<
  number,
  { resolve: (result: Uint8Array) => void; reject: (error: Error) => void }
>();
let clientInitialized = false;
let clientInitialization: Promise<void> | null = null;

export interface ClientInitOptions {
  width?: number;
  height?: number;
  stunServers?: string[];
  turnServers?: string[];
  turnUsername?: string;
  turnCredential?: string;
}

export interface ParsedPacket {
  type: number;
  length: number;
  client_id: number;
  crc32: number;
}

/**
 * Load the WASM module without client-specific initialization.
 * Called early to make help text available for Settings tooltips.
 * Safe to call multiple times - returns early if already loaded.
 */
export async function ensureWasmModuleLoaded(): Promise<void> {
  if (wasmModule) return;
  if (moduleLoading) return moduleLoading;
  moduleLoading = loadWasmModule();
  try {
    await moduleLoading;
  } finally {
    moduleLoading = null;
  }
}

/** Parse credentials in the isolated worker and wipe temporary byte arrays. */
async function withClientCredentials<T>(
  options: ClientCryptoOptions,
  apply: (
    module: ClientModule,
    password: Uint8Array,
    identity: Uint8Array,
    expected: Uint8Array,
  ) => T,
): Promise<T> {
  if (!wasmModule || !clientInitialized)
    throw new Error("Initialize the WASM client before configuring crypto");
  const module = wasmModule;
  const password = new TextEncoder().encode(options.password ?? "");
  let identity: Uint8Array | undefined;
  let expected: Uint8Array | undefined;
  try {
    identity = options.identityPrivateKeyText
      ? await validateSshPrivateKey(options.identityPrivateKeyText)
      : undefined;
    expected = options.expectedServerPublicKeyText
      ? await validateSshPublicKey(options.expectedServerPublicKeyText)
      : undefined;
    if (wasmModule !== module || !clientInitialized)
      throw new Error("WASM client was closed while configuring crypto");
    return apply(
      module,
      password,
      identity ?? new Uint8Array(),
      expected ?? new Uint8Array(),
    );
  } finally {
    password.fill(0);
    identity?.fill(0);
    expected?.fill(0);
  }
}

/** Set browser authentication and identity-verification inputs for the next handshake. */
export async function configureClientCrypto(
  options: ClientCryptoOptions,
): Promise<void> {
  await withClientCredentials(
    options,
    (module, password, identity, expected) => {
      const result = module.configureCrypto(password, identity, expected);
      if (result !== 0)
        throwWasmError(result, "WASM rejected the configured crypto settings");
    },
  );
}
/** Validate an OpenSSH or unencrypted OpenPGP Ed25519 private key with the shared C parsers. */
export async function validateSshPrivateKey(
  contents: string,
): Promise<Uint8Array> {
  try {
    return await parseKeyInWorker("ssh-private", contents);
  } catch {
    try {
      return await parseKeyInWorker("gpg-private", contents);
    } catch (error) {
      throw new Error(
        `Expected an OpenSSH or unencrypted OpenPGP Ed25519 private key: ${error instanceof Error ? error.message : String(error)}`,
        { cause: error },
      );
    }
  }
}

/** Validate an SSH or armored OpenPGP Ed25519 public key with the shared C parsers. */
export async function validateSshPublicKey(
  contents: string,
): Promise<Uint8Array> {
  try {
    return await parseKeyInWorker("ssh-public", contents);
  } catch {
    try {
      return await parseKeyInWorker("gpg-public", contents);
    } catch {
      for (const kind of ["ssh-private", "gpg-private"] as const) {
        try {
          const privateKey = await parseKeyInWorker(kind, contents);
          privateKey.fill(0);
          throw new Error(
            "This is a private key. Add it under Private keys; only public keys are accepted here.",
          );
        } catch (error) {
          if (
            error instanceof Error &&
            error.message.startsWith("This is a private key.")
          ) {
            throw error;
          }
        }
      }
      throw new Error("Expected an SSH or armored OpenPGP Ed25519 public key.");
    }
  }
}

function parseKeyInWorker(
  kind: "ssh-private" | "ssh-public" | "gpg-private" | "gpg-public",
  contents: string,
): Promise<Uint8Array> {
  const worker =
    keyParserWorker ??
    new Worker(new URL("./cryptoKeyParser.worker.ts", import.meta.url), {
      type: "module",
    });
  keyParserWorker = worker;
  const id = nextKeyParserRequestId++;
  return new Promise((resolve, reject) => {
    pendingKeyParserRequests.set(id, { resolve, reject });
    worker.onmessage = (
      event: MessageEvent<{
        id: number;
        bytes?: ArrayBuffer;
        error?: string;
        frames?: BrowserErrorFrame[];
      }>,
    ) => {
      const pending = pendingKeyParserRequests.get(event.data.id);
      if (!pending) return;
      pendingKeyParserRequests.delete(event.data.id);
      if (event.data.error) {
        // Failed parser attempts are expected while trying multiple key formats.
        pending.reject(new Error(event.data.error));
      } else if (event.data.bytes)
        pending.resolve(new Uint8Array(event.data.bytes));
      else pending.reject(new Error("SSH key parser returned no result"));
    };
    worker.onerror = (event) => {
      const error = new Error(event.message || "SSH key parser worker failed");
      for (const request of pendingKeyParserRequests.values())
        request.reject(error);
      pendingKeyParserRequests.clear();
      keyParserWorker?.terminate();
      keyParserWorker = null;
    };
    worker.postMessage({ id, kind, contents });
  });
}

async function loadWasmModule(): Promise<void> {
  if (!globalThis.crossOriginIsolated) {
    throw new Error(
      "WASM requires cross-origin isolation. Serve this page with COOP: same-origin and COEP: require-corp.",
    );
  }

  console.log("[WASM] Loading module for help text...");
  wasmModule = await createWasmModule<ClientModule>("client");

  if (!wasmModule) {
    throw new Error("Failed to load WASM module");
  }

  // Expose WASM module to window for help text access
  const globalWindow = globalThis as typeof globalThis & {
    asciiChatWasm: AsciiChatWasmExports;
  };
  globalWindow.asciiChatWasm = {
    _wasmModule: wasmModule,
    getHelpText: wasmModule.optionHelp.bind(wasmModule),
  };
  console.log("[WASM] Module loaded and help text available");
}

/** The initialized client module, for discovery's terminal renderer. */
export function getClientWasmModule(): ClientModule | null {
  return wasmModule;
}

/**
 * Initialize the WASM module (call once at app start)
 */
export async function initClientWasm(
  options: ClientInitOptions = {},
): Promise<void> {
  if (clientInitialized) return;
  if (clientInitialization) return clientInitialization;

  clientInitialization = initializeClientWasm(options);
  try {
    await clientInitialization;
  } finally {
    clientInitialization = null;
  }
}

async function initializeClientWasm(options: ClientInitOptions): Promise<void> {
  // Ensure WASM module is loaded first (may already be loaded by ensureWasmModuleLoaded)
  await ensureWasmModuleLoaded();

  if (!wasmModule) {
    throw new Error("WASM module failed to load");
  }

  console.log("[Client WASM] Starting module initialization...");
  // WASM module is already loaded, proceed with client-specific initialization

  if (!wasmModule) {
    throw new Error("Failed to load client WASM module");
  }
  console.log("[Client WASM] Module loaded successfully");

  // Build argument string for options_init()
  const args: string[] = ["client"];

  if (options.width !== undefined) {
    args.push("--width", options.width.toString());
  }
  if (options.height !== undefined) {
    args.push("--height", options.height.toString());
  }
  if (options.stunServers?.length)
    args.push("--stun-servers", options.stunServers.join(","));
  if (options.turnServers?.length)
    args.push("--turn-servers", options.turnServers.join(","));
  if (options.turnUsername) args.push("--turn-username", options.turnUsername);
  if (options.turnCredential)
    args.push("--turn-credential", options.turnCredential);
  const argsString = args
    .map((arg, index) =>
      index === 0
        ? arg
        : `"${arg.replace(/\\/g, "\\\\").replace(/"/g, '\\"')}"`,
    )
    .join(" ");
  console.log("[Client WASM] Initializing with args:", argsString);

  const result = wasmModule.initClient(argsString);
  console.log("[Client WASM] _client_init_with_args returned:", result);
  if (result !== 0)
    throwWasmError(result, "Failed to initialize client WASM module");
  clientInitialized = true;
}

/**
 * Cleanup WASM module resources
 */
export function cleanupClientWasm(): void {
  console.log("[cleanupClientWasm] ========== CLEANUP CALLED ==========");
  if (wasmModule) {
    if (clientInitialized) {
      console.log("[cleanupClientWasm] Calling _client_cleanup()...");
      wasmModule._client_cleanup();
      clientInitialized = false;
    }
    // Keep the Emscripten module and its shared memory/pthread pool alive.
    // Recreating it on every WebRTC reconnect leaks worker-backed memories
    // until the browser can no longer allocate another WebAssembly.Memory.

    console.log("[cleanupClientWasm] Client state cleaned up; module retained");
  } else {
    console.log("[cleanupClientWasm] No module to cleanup");
  }
}

/**
 * Generate client keypair for handshake
 */
export async function generateKeypair(): Promise<string> {
  if (!wasmModule) {
    throw new Error(
      "WASM module not initialized. Call initClientWasm() first.",
    );
  }

  console.log("[Client WASM] Generating keypair...");
  const result = wasmModule._client_generate_keypair();
  if (result !== 0) {
    throwWasmError(result, "Failed to generate keypair");
  }

  // Get the public key as hex string
  const publicKeyHex = wasmModule.clientPublicKey();
  if (publicKeyHex === undefined) {
    throw new Error("Failed to retrieve public key");
  }

  console.log(
    `[Client WASM] Keypair generated, pubkey=${publicKeyHex.substring(0, 16)}...`,
  );
  return publicKeyHex;
}

/**
 * Set server address for known_hosts verification
 */
export function setServerAddress(serverHost: string, serverPort: number): void {
  if (!wasmModule) {
    throw new Error(
      "WASM module not initialized. Call initClientWasm() first.",
    );
  }

  console.log(
    `[Client WASM] Setting server address: ${serverHost}:${serverPort}`,
  );

  const result = wasmModule.setServerAddress(serverHost, serverPort);
  if (result !== 0) throwWasmError(result, "Failed to set server address");
}

function handleHandshake(
  stage: number,
  rawPacket: Uint8Array,
  description: string,
): void {
  if (!wasmModule) throw new Error("WASM module not initialized");
  const result = wasmModule.handleHandshake(stage, rawPacket);
  if (result !== 0) throwWasmError(result, `Failed to handle ${description}`);
}

export function handleCryptoParameters(rawPacket: Uint8Array): void {
  handleHandshake(0, rawPacket, "CRYPTO_PARAMETERS");
}
export function handleKeyExchangeInit(rawPacket: Uint8Array): void {
  handleHandshake(1, rawPacket, "KEY_EXCHANGE_INIT");
}
export function handleAuthChallenge(rawPacket: Uint8Array): void {
  handleHandshake(2, rawPacket, "AUTH_CHALLENGE");
}
export function handleHandshakeComplete(rawPacket: Uint8Array): void {
  handleHandshake(3, rawPacket, "HANDSHAKE_COMPLETE");
}
/**
 * Register callback for WASM to send packets back to JavaScript
 * This is called by crypto handshake callbacks when they need to send responses
 */
export function registerSendPacketCallback(
  callback: (rawPacket: Uint8Array) => void,
): void {
  if (!wasmModule) {
    throw new Error("WASM module not initialized");
  }

  console.log("[Client WASM] Registered sendPacketCallback");
  wasmModule.sendPacketCallback = callback;
}

/** Encrypt a complete packet or decrypt an encrypted payload. */
function cryptPacket(encrypt: boolean, packet: Uint8Array): Uint8Array {
  if (!wasmModule) throw new Error("WASM module not initialized");
  const result = wasmModule.cryptPacket(encrypt, packet);
  if (result.code)
    throwWasmError(
      result.code,
      encrypt ? "Encryption failed" : "Decryption failed",
    );
  return result.bytes;
}
export function encryptPacket(plaintext: Uint8Array): Uint8Array {
  return cryptPacket(true, plaintext);
}
export function decryptPacket(ciphertext: Uint8Array): Uint8Array {
  return cryptPacket(false, ciphertext);
}
/** Initialize per-connection QUIC security after the authenticated handshake. */
export function initPacketSecurity(): void {
  if (!wasmModule) throw new Error("WASM module not initialized");
  const result = wasmModule.clientQuicInit();
  if (result !== 0)
    throwWasmError(result, "QUIC security initialization failed");
}

export function getSecurityEpoch(): number {
  return wasmModule?.clientQuicEpoch() ?? -1;
}

/** Runs the shared C rekey coordinator and drains its bounded reliable output. */
export function tickPacketSecurity(): {
  flow: 0 | 1 | 2;
  packet: Uint8Array;
}[] {
  if (!wasmModule) throw new Error("WASM module not initialized");
  const result = wasmModule.clientQuicTick();
  if (result) throwWasmError(result, "QUIC rekey failed");
  const packets: { flow: 0 | 1 | 2; packet: Uint8Array }[] = [];
  for (let i = 0; i <= 16; ++i) {
    const output = wasmModule.clientQuicOutput();
    if (output.code) throwWasmError(output.code, "QUIC rekey output failed");
    if (!output.packet) return packets;
    if (output.flow < 0 || output.flow > 2)
      throw new Error("Invalid QUIC rekey flow");
    packets.push({ flow: output.flow as 0 | 1 | 2, packet: output.packet });
  }
  throw new Error("QUIC rekey output exceeded its bound");
}

/** Authenticate a complete packet and its QUIC flow and delivery metadata. */
export function processSecurePacket(
  operation: "seal" | "open",
  flow: 0 | 1 | 2,
  delivery: 0 | 1,
  packet: Uint8Array,
  quietFailure = false,
): { packet: Uint8Array; sequence: bigint } {
  if (!wasmModule) throw new Error("WASM module not initialized");
  const result = wasmModule.clientQuicPacket(
    operation === "seal",
    flow,
    delivery,
    packet,
  );
  if (result.code !== 0) {
    if (quietFailure && operation === "open" && delivery === 1)
      throw new Error(`QUIC packet ${operation} failed (${result.code})`);
    throwWasmError(result.code, `QUIC packet ${operation} failed`);
  }
  return { packet: result.packet, sequence: result.sequence };
}
/** Parse a validated packet and copy its metadata into JavaScript. */
export function parsePacket(rawPacket: Uint8Array): ParsedPacket {
  if (!wasmModule) throw new Error("WASM module not initialized");
  const json = wasmModule.parsePacketJson(rawPacket);
  if (json === undefined) throw new Error("Failed to parse packet");
  return JSON.parse(json) as ParsedPacket;
}

/** Serialize a packet to a JavaScript-owned array. */
export function serializePacket(
  packetType: number,
  payload: Uint8Array,
  clientId: number,
): Uint8Array {
  if (!wasmModule) throw new Error("WASM module not initialized");
  const result = wasmModule.serializePacket(packetType, payload, clientId);
  if (result.code) throwWasmError(result.code, "Serialization failed");
  return result.bytes;
}
/**
 * Send a video frame (placeholder for now)
 */
export async function sendVideoFrame(
  rgbaData: Uint8Array,
  width: number,
  height: number,
): Promise<void> {
  if (!wasmModule) {
    throw new Error("WASM module not initialized");
  }

  const result = wasmModule.sendClientFrame(rgbaData, width, height);
  if (result !== 0) throwWasmError(result, "Failed to send video frame");
}

/**
 * Get current connection state
 */
export function getConnectionState(): ConnectionState {
  if (!wasmModule) {
    throw new Error("WASM module not initialized");
  }
  return wasmModule._client_get_connection_state();
}

/**
 * Set color mode
 */
export function setColorMode(mode: ColorMode): void {
  if (!wasmModule) throw new Error("WASM module not initialized");

  const result = wasmModule._set_color_mode(mode);
  if (result !== 0) throwWasmError(result, `Invalid color mode: ${mode}`);
}

/**
 * Get current color mode
 */
export function getColorMode(): ColorMode {
  if (!wasmModule) throw new Error("WASM module not initialized");
  return wasmModule._get_color_mode();
}

/**
 * Set color filter
 */
export function setColorFilter(filter: ColorFilter): void {
  if (!wasmModule) throw new Error("WASM module not initialized");

  const result = wasmModule._set_color_filter(filter);
  if (result !== 0) throwWasmError(result, `Invalid color filter: ${filter}`);
}

/**
 * Get current color filter
 */
export function getColorFilter(): ColorFilter {
  if (!wasmModule) throw new Error("WASM module not initialized");
  return wasmModule._get_color_filter();
}

/**
 * Set palette
 */
export function setPalette(palette: Palette): void {
  if (!wasmModule) throw new Error("WASM module not initialized");

  const result = wasmModule.setPalette(palette);
  if (result !== 0) throwWasmError(result, `Failed to set palette: ${palette}`);
}

/**
 * Get current palette
 */
export function getPalette(): string {
  if (!wasmModule) throw new Error("WASM module not initialized");
  const json = wasmModule.readOptions();
  if (!json) return "";
  const values = (JSON.parse(json) as { values: Record<string, unknown> })
    .values;
  const names: Palette[] = [
    "standard",
    "standard",
    "blocks",
    "digital",
    "minimal",
    "cool",
    "custom",
  ];
  return names[Number(values["palette_type"])] ?? "standard";
}

/**
 * Set custom palette characters
 */
export function setPaletteChars(chars: string): void {
  if (!wasmModule) throw new Error("WASM module not initialized");

  const result = wasmModule.setPaletteChars(chars);
  if (result !== 0)
    throwWasmError(result, `Failed to set palette chars: ${chars}`);
}

/**
 * Get current custom palette characters
 */
export function getPaletteChars(): string {
  if (!wasmModule) throw new Error("WASM module not initialized");
  return wasmModule.getPaletteChars();
}

/**
 * Set Matrix rain effect
 */
export function setMatrixRain(enabled: boolean): void {
  if (!wasmModule) throw new Error("WASM module not initialized");

  const result = wasmModule._set_matrix_rain(enabled ? 1 : 0);
  if (result !== 0)
    throwWasmError(result, `Failed to set matrix rain: ${enabled}`);
}

/**
 * Get current matrix rain state
 */
export function getMatrixRain(): boolean {
  if (!wasmModule) throw new Error("WASM module not initialized");
  return wasmModule._get_matrix_rain() !== 0;
}

/**
 * Set flip X (horizontal flip)
 */
export function setFlipX(enabled: boolean): void {
  if (!wasmModule) throw new Error("WASM module not initialized");

  const result = wasmModule._set_flip_x(enabled ? 1 : 0);
  if (result !== 0) throwWasmError(result, `Failed to set flip X: ${enabled}`);
}

/**
 * Get current flip X state
 */
export function getFlipX(): boolean {
  if (!wasmModule) throw new Error("WASM module not initialized");
  return wasmModule._get_flip_x() !== 0;
}

/**
 * Set target FPS
 */
export function setTargetFps(fps: number): void {
  if (!wasmModule) throw new Error("WASM module not initialized");

  const result = wasmModule._set_target_fps(fps);
  if (result !== 0) throwWasmError(result, `Invalid target FPS: ${fps}`);
}

/**
 * Get current target FPS
 */
export function getTargetFps(): number {
  if (!wasmModule) throw new Error("WASM module not initialized");
  return wasmModule._get_target_fps();
}

/**
 * Set ASCII width (columns)
 */
export function setWidth(width: number): void {
  if (!wasmModule) throw new Error("WASM module not initialized");

  const result = wasmModule._set_width(width);
  if (result !== 0) throwWasmError(result, `Invalid width: ${width}`);
}

/**
 * Get current ASCII width
 */
export function getWidth(): number {
  if (!wasmModule) throw new Error("WASM module not initialized");
  return wasmModule._get_width();
}

/**
 * Set ASCII height (rows)
 */
export function setHeight(height: number): void {
  if (!wasmModule) throw new Error("WASM module not initialized");

  const result = wasmModule._set_height(height);
  if (result !== 0) throwWasmError(result, `Invalid height: ${height}`);
}

/**
 * Get current ASCII height
 */
export function getHeight(): number {
  if (!wasmModule) throw new Error("WASM module not initialized");
  return wasmModule._get_height();
}

/**
 * Check if WASM module is ready
 */
export function isWasmReady(): boolean {
  return wasmModule !== null && wasmModule.HEAPU8 !== undefined;
}

/**
 * Get the WASM module instance
 */
export function getClientModule(): ClientModule | null {
  return wasmModule;
}

/** Uses the shared C policy after configureClientCrypto has installed credentials. */
export function resolveCryptoPolicy(
  serverUrl: string,
  encryption?: boolean,
): number {
  if (!wasmModule) throw new Error("WASM client is not initialized");
  const mode = wasmModule._client_resolve_crypto_policy(
    new URL(serverUrl).protocol === "wss:" ? 1 : 0,
    encryption === undefined ? -1 : encryption ? 1 : 0,
  );
  if (mode < 0) throw new Error("Invalid crypto policy");
  return mode;
}

/** Separate ACIP context for one media peer; signaling retains its own context. */
export class PeerCryptoSession {
  private securityTimer: ReturnType<typeof setInterval> | null = null;
  private independentChannels = false;
  private constructor(
    private binding: PeerCryptoBinding | null,
    private send: (packet: Uint8Array) => void,
  ) {}

  private getBinding(): PeerCryptoBinding {
    if (!this.binding) throw new Error("Peer crypto session is closed");
    return this.binding;
  }

  static async create(
    options: ClientCryptoOptions,
    encryption: boolean | undefined,
    server: boolean,
    send: (packet: Uint8Array) => void,
  ): Promise<PeerCryptoSession> {
    const credentials = { ...options };
    if (server) {
      delete credentials.expectedServerPublicKeyText;
      if (options.allowedClientPublicKeyText)
        credentials.expectedServerPublicKeyText =
          options.allowedClientPublicKeyText;
    }
    return withClientCredentials(
      credentials,
      (module, password, identity, expected) => {
        const binding = new module.PeerCryptoBinding(
          server,
          encryption === undefined ? -1 : encryption ? 1 : 0,
          password,
          identity,
          expected,
        );
        if (!binding.valid()) {
          const message = binding.error();
          binding.delete();
          throwWasmError(-1, message);
        }
        return new PeerCryptoSession(binding, send);
      },
    );
  }

  beginQuic(): void {
    this.independentChannels = true;
    const result = this.getBinding().beginQuic();
    if (result) throwWasmError(result, "Cannot start peer QUIC handshake");
  }

  initQuic(): void {
    const result = this.getBinding().initQuic();
    if (result) throwWasmError(result, "Peer QUIC initialization failed");
  }

  get quicEpoch() {
    return this.binding?.quicEpoch() ?? -1;
  }

  tickQuic() {
    const binding = this.getBinding();
    const status = binding.tickQuic();
    if (status) throwWasmError(status, "QUIC rekey failed");
    const packets: { flow: 0 | 1 | 2; packet: Uint8Array }[] = [];
    for (let i = 0; i <= 16; ++i) {
      const output = binding.takeQuicOutput();
      if (output.code) throwWasmError(output.code, "QUIC rekey output failed");
      if (!output.packet) return packets;
      if (output.flow < 0 || output.flow > 2)
        throw new Error("Invalid QUIC rekey flow");
      packets.push({ flow: output.flow as 0 | 1 | 2, packet: output.packet });
    }
    throw new Error("QUIC rekey output exceeded its bound");
  }

  quicPacket(
    operation: "seal" | "open",
    flow: 0 | 1 | 2,
    delivery: 0 | 1,
    packet: Uint8Array,
  ) {
    const result = this.getBinding().quicPacket(
      operation === "seal",
      flow,
      delivery,
      packet,
    );
    if (result.code) {
      if (operation === "open" && delivery === 1)
        throw new Error(`QUIC packet ${operation} failed (${result.code})`);
      throwWasmError(result.code, `QUIC packet ${operation} failed`);
    }
    return { packet: result.packet, sequence: result.sequence };
  }

  get ready(): boolean {
    return this.binding?.ready() ?? false;
  }

  flush(): void {
    if (this.ready && !this.independentChannels && !this.securityTimer) {
      this.securityTimer = setInterval(() => {
        try {
          this.flush();
        } catch (error) {
          this.close();
          publishBrowserError({
            source: "C/WASM",
            title: "Peer security failed",
            frames: [{ message: String(error) }],
          });
        }
      }, 100);
    }
    if (this.binding) {
      const result = this.binding.tick();
      if (result) throwWasmError(result, "Peer rekey failed");
    }
    while (this.binding) {
      const packet = this.binding.takeOutput();
      if (!packet) break;
      this.send(packet);
    }
  }

  private input(packet: Uint8Array, receive: boolean): void {
    const binding = this.getBinding();
    const result = receive ? binding.receive(packet) : binding.send(packet);
    if (result !== 0) throwWasmError(result, binding.error());
    this.flush();
  }

  receive(packet: Uint8Array): Uint8Array | null {
    this.input(packet, true);
    return this.binding?.received() ?? null;
  }

  sendPacket(packet: Uint8Array): void {
    this.input(packet, false);
  }

  close(): void {
    if (this.securityTimer) clearInterval(this.securityTimer);
    this.securityTimer = null;
    this.binding?.delete();
    this.binding = null;
  }
}

/** Wire decoding stays in the transport adapter; rendered-frame policy lives in C. */
export function validateReceivedFrame(
  bytes: Uint8Array,
  width: number,
  height: number,
  checksum: number,
  flags: number,
  metadata: Uint8Array,
  originalSize: number,
): Uint8Array {
  if (
    !bytes.length ||
    bytes.length > 16 * 1024 * 1024 ||
    !Number.isSafeInteger(originalSize) ||
    originalSize <= 0 ||
    originalSize > 16 * 1024 * 1024 ||
    metadata.length > 275
  )
    throw new Error("Received frame allocation exceeds byte budget");
  const compressed = (flags & 2) !== 0;
  if (!compressed && bytes.length !== originalSize)
    throw new Error("Uncompressed frame size mismatch");
  if (!wasmModule) throw new Error("Client frame validator is unavailable");
  const result = wasmModule.validateFrame(
    bytes,
    width,
    height,
    checksum,
    flags,
    metadata,
    originalSize,
  );
  if (result.code !== 0)
    throwWasmError(
      result.code,
      result.decodeFailed
        ? "Decompressed ASCII frame size mismatch"
        : "Invalid received rendered frame",
    );
  return result.bytes;
}
