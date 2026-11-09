// TypeScript wrapper for Client WASM module
// Provides type-safe interface to libasciichat client mode

import type {
  RuntimeValues,
  ClientWasmBindings,
  BrowserSessionBinding,
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

type ClientModule = ClientWasmBindings & {
  hostPacketCallback?: (id: number, bytes: Uint8Array) => void;
  discoverySend?: (handle: number, type: number, payload: Uint8Array) => void;
};
/** Publish the complete pending C error stack and throw at the JS boundary. */
export function throwWasmError(result: number, fallback: string): never {
  const module = wasmModule;
  const frames: BrowserErrorFrame[] = [];
  if (module) {
    const total = module.ErrorBinding.snapshot();
    const count = Math.min(total, 64);
    for (let index = 0; index < count; index++) {
      const frame = module.ErrorBinding.frame(index);
      frames.push({ ...frame, message: frame.message || fallback });
    }
  }
  publishBrowserError({
    source: "C/WASM",
    title: `${fallback} (${result})`,
    frames: frames.length ? frames : [{ code: result, message: fallback }],
    dismiss: () => module?.ErrorBinding.dismiss(),
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
  if (!wasmModule || !clientSession)
    throw new Error("Client engine is unavailable");
  const result = clientSession.settingsReceive(payload);
  if (result.code) throwWasmError(result.code, "Invalid remote settings");
  if (result.json === null)
    throw new Error("Cannot read effective remote settings");
  return JSON.parse(result.json) as RuntimeValues;
}

/** Read the effective remote settings owned by the active client session. */
export function currentRemoteRenderSettings(): RuntimeValues {
  if (!clientSession) throw new Error("Client engine is unavailable");
  const json = clientSession.settingsValues().json;
  if (json === null) throw new Error("Cannot read effective remote settings");
  return JSON.parse(json) as RuntimeValues;
}

// Enums matching libasciichat definitions
export enum ConnectionState {
  DISCONNECTED = 0,
  CONNECTING = 1,
  HANDSHAKE = 2,
  CONNECTED = 3,
  ERROR = 4,
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
  QUIC_READY = 1110,
  QUIC_KEYFRAME_REQUEST = 1111,
  CRYPTO_EPOCH_MARKER = 1112,
  CRYPTO_REKEY_NEEDED = 1113,
  CRYPTO_EPOCH_ACK = 1114,

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
    [PacketType.QUIC_READY]: "QUIC_READY",
    [PacketType.QUIC_KEYFRAME_REQUEST]: "QUIC_KEYFRAME_REQUEST",
    [PacketType.CRYPTO_EPOCH_MARKER]: "CRYPTO_EPOCH_MARKER",
    [PacketType.CRYPTO_REKEY_NEEDED]: "CRYPTO_REKEY_NEEDED",
    [PacketType.CRYPTO_EPOCH_ACK]: "CRYPTO_EPOCH_ACK",
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
let clientSession: InstanceType<ClientModule["ClientSessionBinding"]> | null =
  null;
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
    (_module, password, identity, expected) => {
      const result = processClientEvent({
        type: "configure-crypto",
        password,
        identity,
        expected,
      });
      if (result.code)
        throwWasmError(
          result.code,
          "WASM rejected the configured crypto settings",
        );
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

  const initialOptions: Record<string, number | string | boolean> = {};
  if (options.width !== undefined) initialOptions["width"] = options.width;
  if (options.height !== undefined) initialOptions["height"] = options.height;
  if (options.stunServers?.length)
    initialOptions["stun_servers"] = options.stunServers.join(",");
  if (options.turnServers?.length)
    initialOptions["turn_servers"] = options.turnServers.join(",");
  if (options.turnUsername)
    initialOptions["turn_username"] = options.turnUsername;
  if (options.turnCredential)
    initialOptions["turn_credential"] = options.turnCredential;

  const session = new wasmModule.ClientSessionBinding(initialOptions);
  if (!session.valid()) {
    const result = session.error();
    session.delete();
    throwWasmError(result, "Failed to initialize client WASM module");
  }
  clientSession = session;
  clientInitialized = true;
  console.log("[Client WASM] Session initialized");
}

/**
 * Cleanup WASM module resources
 */
export function cleanupClientWasm(): void {
  console.log("[cleanupClientWasm] ========== CLEANUP CALLED ==========");
  if (wasmModule) {
    if (clientInitialized) {
      clientSession?.close();
      clientSession?.delete();
      clientSession = null;
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
  const result = checkClientEvent(
    processClientEvent({ type: "generate-keypair" }),
    "Failed to generate keypair",
  );
  const publicKeyHex = result.publicKey;
  if (!publicKeyHex) throw new Error("Failed to retrieve public key");

  console.log(
    `[Client WASM] Keypair generated, pubkey=${publicKeyHex.substring(0, 16)}...`,
  );
  return publicKeyHex;
}

export type ClientEventResult = {
  ok?: boolean;
  code: number;
  state: ConnectionState;
  message?: string;
  retryable?: boolean;
  bytes?: Uint8Array;
  packet?: ParsedPacket;
  outputs?: { flow: 0 | 1 | 2; packet: Uint8Array }[];
  epoch?: number;
  sequence?: bigint;
  publicKey?: string;
  mode?: number;
  capabilities?: {
    webtransport: boolean;
    datagrams: boolean;
    hevc: boolean;
    webrtc: boolean;
  };
  ready?: boolean;
  queuedBytes?: number;
  writable?: boolean;
  action?: {
    kind: number;
    participant: Uint8Array;
    generation: number;
    sequence: number;
  } | null;
};

/** The single coarse client/session ABI for browser events and native effects. */
export function processClientEvent(
  event: Record<string, unknown>,
): ClientEventResult {
  if (!clientSession || !clientInitialized)
    throw new Error("WASM client session is not initialized");
  return clientSession.process(event) as ClientEventResult;
}

/** Pass browser-detected facts to the native session policy layer. */
export function configureBrowserCapabilities(capabilities: {
  webtransport: boolean;
  datagrams: boolean;
  hevc: boolean;
  webrtc: boolean;
}): ClientEventResult {
  return processClientEvent({
    type: "configure-browser-capabilities",
    ...capabilities,
  });
}

/** Process a bounded burst of browser events in one native session call. */
export function processClientEvents(
  events: readonly Record<string, unknown>[],
): ClientEventResult[] {
  if (!clientSession || !clientInitialized)
    throw new Error("WASM client session is not initialized");
  return clientSession.processBatch(events) as ClientEventResult[];
}

/** Drain pending native transport output without synthesizing a browser event. */
export function drainClientOutputs(): ClientEventResult {
  if (!clientSession || !clientInitialized)
    throw new Error("WASM client session is not initialized");
  return clientSession.drainOutputs() as ClientEventResult;
}

function checkClientEvent(
  result: ClientEventResult,
  fallback: string,
): ClientEventResult {
  if (result.code) throwWasmError(result.code, fallback);
  return result;
}

/**
 * Check if WASM module is ready
 */
export function isWasmReady(): boolean {
  return wasmModule !== null;
}

/**
 * Get the WASM module instance
 */
export function getClientModule(): ClientModule | null {
  return wasmModule;
}

/** Separate ACIP context for one media peer; signaling retains its own context. */
export class BrowserSession {
  private securityTimer: ReturnType<typeof setInterval> | null = null;
  private independentChannels = false;
  private constructor(
    private binding: BrowserSessionBinding | null,
    private send: (packet: Uint8Array) => void,
    private pendingOutputs: Uint8Array[] = [],
  ) {}

  private getBinding(): BrowserSessionBinding {
    if (!this.binding) throw new Error("Browser security session is closed");
    return this.binding;
  }

  /** Submit a bounded burst of transport events through one native call. */
  processBatch(
    events: readonly Record<string, unknown>[],
  ): Record<string, unknown>[] {
    return this.getBinding().processBatch(events) as Record<string, unknown>[];
  }

  static async create(
    options: ClientCryptoOptions,
    encryption: boolean | undefined,
    server: boolean,
    send: (packet: Uint8Array) => void,
  ): Promise<BrowserSession> {
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
        const binding = new module.BrowserSessionBinding(
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
        const startedResult = binding.process({ type: "start" }) as {
          code: number;
          outputs?: Uint8Array[];
        };
        const initialOutputs = startedResult.outputs ?? [];
        const started = startedResult.code;
        if (started) {
          const message = binding.error();
          binding.delete();
          throwWasmError(started, message);
        }
        // Keep startup packets inside the session until its owner has wired
        // the browser transport and explicitly flushes it.
        return new BrowserSession(binding, send, initialOutputs);
      },
    );
  }

  beginQuic(): void {
    this.independentChannels = true;
    const result = this.getBinding().process({ type: "quic-begin" })[
      "code"
    ] as number;
    if (result) throwWasmError(result, "Cannot start peer QUIC handshake");
  }

  initQuic(): void {
    const result = this.getBinding().process({ type: "quic-init" })[
      "code"
    ] as number;
    if (result) throwWasmError(result, "Peer QUIC initialization failed");
  }

  get quicEpoch() {
    return (
      (this.binding?.process({ type: "quic-epoch" })["epoch"] as
        | number
        | undefined) ?? -1
    );
  }

  tickQuic() {
    const binding = this.getBinding();
    const response = binding.process({ type: "quic-tick" }) as {
      code: number;
      quicOutputs?: Array<{ flow: number; packet: Uint8Array | null }>;
    };
    const status = response.code;
    if (status) throwWasmError(status, "QUIC rekey failed");
    const packets: { flow: 0 | 1 | 2; packet: Uint8Array }[] = [];
    for (const output of response.quicOutputs ?? []) {
      if (!output.packet) continue;
      if (output.flow < 0 || output.flow > 2)
        throw new Error("Invalid QUIC rekey flow");
      packets.push({ flow: output.flow as 0 | 1 | 2, packet: output.packet });
    }
    return packets;
  }

  quicPacket(
    operation: "seal" | "open",
    flow: 0 | 1 | 2,
    delivery: 0 | 1,
    packet: Uint8Array,
  ) {
    const result = this.getBinding().process({
      type: "quic-packet",
      seal: operation === "seal",
      flow,
      delivery,
      packet,
    }) as { code: number; packet: Uint8Array; sequence: bigint };
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
      for (const packet of this.pendingOutputs.splice(0)) this.send(packet);
      const result = this.binding.process({ type: "tick" }) as {
        code: number;
        outputs?: Uint8Array[];
      };
      if (result.code) throwWasmError(result.code, "Peer rekey failed");
      for (const packet of result.outputs ?? []) this.send(packet);
    }
  }

  private input(packet: Uint8Array, receive: boolean): Record<string, unknown> {
    const binding = this.getBinding();
    const result = binding.process({
      type: receive ? "receive" : "send",
      packet,
    }) as { code: number; outputs?: Uint8Array[] };
    const code = result["code"] as number;
    if (code !== 0) throwWasmError(code, binding.error());
    for (const output of result.outputs ?? []) this.send(output);
    this.flush();
    return result as Record<string, unknown>;
  }

  receive(packet: Uint8Array): Uint8Array | null {
    return (this.input(packet, true)["received"] as Uint8Array | null) ?? null;
  }

  sendPacket(packet: Uint8Array): void {
    this.input(packet, false);
  }

  close(): void {
    if (this.securityTimer) clearInterval(this.securityTimer);
    this.securityTimer = null;
    this.binding?.close();
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
  if (!wasmModule || !clientSession)
    throw new Error("Client frame validator is unavailable");
  const result = clientSession.validateFrame(
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
