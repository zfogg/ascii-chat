// TypeScript wrapper for Client WASM module
// Provides type-safe interface to libasciichat client mode

// Type definition for the Emscripten module
interface ClientModuleExports {
  _client_init_with_args(args_string: number): number;
  _client_cleanup(): void;
  _client_generate_keypair(): number;
  _client_set_server_address(server_host: number, server_port: number): number;
  _client_get_public_key_hex(): number;
  _client_handle_key_exchange_init(packet_ptr: number, packet_len: number): number;
  _client_handle_auth_challenge(packet_ptr: number, packet_len: number): number;
  _client_handle_handshake_complete(packet_ptr: number, packet_len: number): number;
  _client_encrypt_packet(pt_ptr: number, pt_len: number, ct_ptr: number, out_len_ptr: number): number;
  _client_decrypt_packet(ct_ptr: number, ct_len: number, pt_ptr: number, out_len_ptr: number): number;
  _client_parse_packet(pkt_ptr: number, len: number): number;
  _client_serialize_packet(type: number, payload_ptr: number, payload_len: number, client_id: number, output_ptr: number, out_len_ptr: number): number;
  _client_send_video_frame(rgba_ptr: number, width: number, height: number): number;
  _client_get_connection_state(): number;
  _client_free_string(ptr: number): void;
  _malloc(size: number): number;
  _free(ptr: number): void;
}

interface ClientModule {
  HEAPU8: Uint8Array;
  HEAP32: Int32Array;
  UTF8ToString(ptr: number): string;
  stringToUTF8(str: string, outPtr: number, maxBytesToWrite: number): void;
  lengthBytesUTF8(str: string): number;
  sendPacketCallback?: (rawPacket: Uint8Array) => void;
  _client_init_with_args: ClientModuleExports['_client_init_with_args'];
  _client_cleanup: ClientModuleExports['_client_cleanup'];
  _client_generate_keypair: ClientModuleExports['_client_generate_keypair'];
  _client_set_server_address: ClientModuleExports['_client_set_server_address'];
  _client_get_public_key_hex: ClientModuleExports['_client_get_public_key_hex'];
  _client_handle_key_exchange_init: ClientModuleExports['_client_handle_key_exchange_init'];
  _client_handle_auth_challenge: ClientModuleExports['_client_handle_auth_challenge'];
  _client_handle_handshake_complete: ClientModuleExports['_client_handle_handshake_complete'];
  _client_encrypt_packet: ClientModuleExports['_client_encrypt_packet'];
  _client_decrypt_packet: ClientModuleExports['_client_decrypt_packet'];
  _client_parse_packet: ClientModuleExports['_client_parse_packet'];
  _client_serialize_packet: ClientModuleExports['_client_serialize_packet'];
  _client_send_video_frame: ClientModuleExports['_client_send_video_frame'];
  _client_get_connection_state: ClientModuleExports['_client_get_connection_state'];
  _client_free_string: ClientModuleExports['_client_free_string'];
  _malloc: ClientModuleExports['_malloc'];
  _free: ClientModuleExports['_free'];
}

// Enums matching libasciichat definitions
export enum ConnectionState {
  DISCONNECTED = 0,
  CONNECTING = 1,
  HANDSHAKE = 2,
  CONNECTED = 3,
  ERROR = 4
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

  // Rekeying
  CRYPTO_REKEY_REQUEST = 1201,
  CRYPTO_REKEY_RESPONSE = 1202,

  // Session data (encrypted after handshake)
  ENCRYPTED = 13,

  // Audio/Video
  AUDIO_BATCH = 5,
  AUDIO_OPUS_BATCH = 6,
  IMAGE_FRAME = 7,
  ASCII_FRAME = 8,

  // Control
  CLIENT_INFO = 2,
  SERVER_STATUS = 3,
  ERROR_MESSAGE = 4,
  PING = 9,
  PONG = 10
}

// Import the Emscripten-generated module factory
// @ts-expect-error - Generated file without types
import ClientModuleFactory from './dist/client.js';

let wasmModule: ClientModule | null = null;

export interface ClientInitOptions {
  width?: number;
  height?: number;
}

export interface ParsedPacket {
  type: number;
  length: number;
  client_id: number;
  crc32: number;
}

/**
 * Initialize the WASM module (call once at app start)
 */
export async function initClientWasm(options: ClientInitOptions = {}): Promise<void> {
  if (wasmModule) return;

  console.log('[Client WASM] Starting module factory...');
  // Provide runtime environment functions for Emscripten
  wasmModule = await ClientModuleFactory({
    // libsodium crypto random - returns 32-bit unsigned integer
    getRandomValue: function() {
      const buf = new Uint32Array(1);
      crypto.getRandomValues(buf);
      return buf[0];
    }
  });
  console.log('[Client WASM] Module factory completed');

  if (!wasmModule) {
    throw new Error('Failed to load client WASM module');
  }
  console.log('[Client WASM] Module loaded successfully');

  // Build argument string for options_init()
  const args: string[] = ['client'];

  if (options.width !== undefined) {
    args.push('--width', options.width.toString());
  }
  if (options.height !== undefined) {
    args.push('--height', options.height.toString());
  }

  const argsString = args.join(' ');
  console.log('[Client WASM] Initializing with args:', argsString);

  // Allocate string in WASM memory
  const strLen = wasmModule.lengthBytesUTF8(argsString) + 1;
  const strPtr = wasmModule._malloc(strLen);
  if (!strPtr) {
    throw new Error('Failed to allocate memory for args string');
  }

  try {
    wasmModule.stringToUTF8(argsString, strPtr, strLen);

    console.log('[Client WASM] Calling _client_init_with_args...');
    const result = wasmModule._client_init_with_args(strPtr);
    console.log('[Client WASM] _client_init_with_args returned:', result);
    if (result !== 0) {
      throw new Error('Failed to initialize client WASM module');
    }
    console.log('[Client WASM] Initialization complete!');
  } finally {
    wasmModule._free(strPtr);
  }
}

/**
 * Cleanup WASM module resources
 */
export function cleanupClientWasm(): void {
  if (wasmModule) {
    wasmModule._client_cleanup();
    wasmModule = null;
  }
}

/**
 * Generate client keypair for handshake
 */
export async function generateKeypair(): Promise<string> {
  if (!wasmModule) {
    throw new Error('WASM module not initialized. Call initClientWasm() first.');
  }

  const result = wasmModule._client_generate_keypair();
  if (result !== 0) {
    throw new Error('Failed to generate keypair');
  }

  // Get the public key as hex string
  const pubkeyPtr = wasmModule._client_get_public_key_hex();
  if (!pubkeyPtr) {
    throw new Error('Failed to retrieve public key');
  }

  const publicKeyHex = wasmModule.UTF8ToString(pubkeyPtr);
  wasmModule._client_free_string(pubkeyPtr);

  return publicKeyHex;
}

/**
 * Set server address for known_hosts verification
 */
export function setServerAddress(serverHost: string, serverPort: number): void {
  if (!wasmModule) {
    throw new Error('WASM module not initialized. Call initClientWasm() first.');
  }

  // Allocate string for server host
  const hostLen = wasmModule.lengthBytesUTF8(serverHost) + 1;
  const hostPtr = wasmModule._malloc(hostLen);
  if (!hostPtr) {
    throw new Error('Failed to allocate memory for server host');
  }

  try {
    wasmModule.stringToUTF8(serverHost, hostPtr, hostLen);
    const result = wasmModule._client_set_server_address(hostPtr, serverPort);
    if (result !== 0) {
      throw new Error('Failed to set server address');
    }
  } finally {
    wasmModule._free(hostPtr);
  }
}

/**
 * Handle CRYPTO_KEY_EXCHANGE_INIT packet from server
 * This triggers the transport-abstracted crypto handshake flow
 */
export function handleKeyExchangeInit(rawPacket: Uint8Array): void {
  if (!wasmModule) {
    throw new Error('WASM module not initialized');
  }

  // Allocate memory for packet
  const packetPtr = wasmModule._malloc(rawPacket.length);
  try {
    // Copy packet to WASM memory
    wasmModule.HEAPU8.set(rawPacket, packetPtr);

    // Call WASM handshake callback
    const result = wasmModule._client_handle_key_exchange_init(packetPtr, rawPacket.length);
    if (result !== 0) {
      throw new Error('Failed to handle KEY_EXCHANGE_INIT');
    }
  } finally {
    wasmModule._free(packetPtr);
  }
}

/**
 * Handle CRYPTO_AUTH_CHALLENGE packet from server
 */
export function handleAuthChallenge(rawPacket: Uint8Array): void {
  if (!wasmModule) {
    throw new Error('WASM module not initialized');
  }

  const packetPtr = wasmModule._malloc(rawPacket.length);
  try {
    wasmModule.HEAPU8.set(rawPacket, packetPtr);

    const result = wasmModule._client_handle_auth_challenge(packetPtr, rawPacket.length);
    if (result !== 0) {
      throw new Error('Failed to handle AUTH_CHALLENGE');
    }
  } finally {
    wasmModule._free(packetPtr);
  }
}

/**
 * Handle CRYPTO_HANDSHAKE_COMPLETE packet from server
 */
export function handleHandshakeComplete(rawPacket: Uint8Array): void {
  if (!wasmModule) {
    throw new Error('WASM module not initialized');
  }

  const packetPtr = wasmModule._malloc(rawPacket.length);
  try {
    wasmModule.HEAPU8.set(rawPacket, packetPtr);

    const result = wasmModule._client_handle_handshake_complete(packetPtr, rawPacket.length);
    if (result !== 0) {
      throw new Error('Failed to handle HANDSHAKE_COMPLETE');
    }
  } finally {
    wasmModule._free(packetPtr);
  }
}

/**
 * Register callback for WASM to send packets back to JavaScript
 * This is called by crypto handshake callbacks when they need to send responses
 */
export function registerSendPacketCallback(callback: (rawPacket: Uint8Array) => void): void {
  if (!wasmModule) {
    throw new Error('WASM module not initialized');
  }

  wasmModule.sendPacketCallback = callback;
}

/**
 * Encrypt a packet
 */
export function encryptPacket(plaintext: Uint8Array): Uint8Array {
  if (!wasmModule) {
    throw new Error('WASM module not initialized');
  }

  // Allocate buffers
  const ptPtr = wasmModule._malloc(plaintext.length);
  const ctPtr = wasmModule._malloc(plaintext.length + 32); // Room for AEAD overhead
  const outLenPtr = wasmModule._malloc(4); // 32-bit size_t

  if (!ptPtr || !ctPtr || !outLenPtr) {
    if (ptPtr) wasmModule._free(ptPtr);
    if (ctPtr) wasmModule._free(ctPtr);
    if (outLenPtr) wasmModule._free(outLenPtr);
    throw new Error('Failed to allocate memory for encryption');
  }

  try {
    // Copy plaintext to WASM memory
    wasmModule.HEAPU8.set(plaintext, ptPtr);

    // Call encryption
    const result = wasmModule._client_encrypt_packet(
      ptPtr, plaintext.length,
      ctPtr, outLenPtr
    );

    if (result !== 0) {
      throw new Error('Encryption failed');
    }

    // Read output length
    const outLen = wasmModule.HEAP32[outLenPtr >> 2];

    // Copy ciphertext from WASM memory
    const ciphertext = new Uint8Array(outLen);
    ciphertext.set(wasmModule.HEAPU8.subarray(ctPtr, ctPtr + outLen));

    return ciphertext;
  } finally {
    wasmModule._free(ptPtr);
    wasmModule._free(ctPtr);
    wasmModule._free(outLenPtr);
  }
}

/**
 * Decrypt a packet
 */
export function decryptPacket(ciphertext: Uint8Array): Uint8Array {
  if (!wasmModule) {
    throw new Error('WASM module not initialized');
  }

  // Allocate buffers
  const ctPtr = wasmModule._malloc(ciphertext.length);
  const ptPtr = wasmModule._malloc(ciphertext.length); // Plaintext will be smaller
  const outLenPtr = wasmModule._malloc(4); // 32-bit size_t

  if (!ctPtr || !ptPtr || !outLenPtr) {
    if (ctPtr) wasmModule._free(ctPtr);
    if (ptPtr) wasmModule._free(ptPtr);
    if (outLenPtr) wasmModule._free(outLenPtr);
    throw new Error('Failed to allocate memory for decryption');
  }

  try {
    // Copy ciphertext to WASM memory
    wasmModule.HEAPU8.set(ciphertext, ctPtr);

    // Call decryption
    const result = wasmModule._client_decrypt_packet(
      ctPtr, ciphertext.length,
      ptPtr, outLenPtr
    );

    if (result !== 0) {
      throw new Error('Decryption failed');
    }

    // Read output length
    const outLen = wasmModule.HEAP32[outLenPtr >> 2];

    // Copy plaintext from WASM memory
    const plaintext = new Uint8Array(outLen);
    plaintext.set(wasmModule.HEAPU8.subarray(ptPtr, ptPtr + outLen));

    return plaintext;
  } finally {
    wasmModule._free(ctPtr);
    wasmModule._free(ptPtr);
    wasmModule._free(outLenPtr);
  }
}

/**
 * Parse a raw packet and extract metadata
 */
export function parsePacket(rawPacket: Uint8Array): ParsedPacket {
  if (!wasmModule) {
    throw new Error('WASM module not initialized');
  }

  const pktPtr = wasmModule._malloc(rawPacket.length);
  if (!pktPtr) {
    throw new Error('Failed to allocate memory for packet');
  }

  try {
    // Copy packet to WASM memory
    wasmModule.HEAPU8.set(rawPacket, pktPtr);

    // Call parse function
    const jsonPtr = wasmModule._client_parse_packet(pktPtr, rawPacket.length);
    if (!jsonPtr) {
      throw new Error('Failed to parse packet');
    }

    // Read JSON string
    const jsonStr = wasmModule.UTF8ToString(jsonPtr);
    wasmModule._client_free_string(jsonPtr);

    // Parse JSON
    return JSON.parse(jsonStr) as ParsedPacket;
  } finally {
    wasmModule._free(pktPtr);
  }
}

/**
 * Serialize a packet structure to raw bytes
 */
export function serializePacket(
  packetType: number,
  payload: Uint8Array,
  clientId: number
): Uint8Array {
  if (!wasmModule) {
    throw new Error('WASM module not initialized');
  }

  const payloadPtr = payload.length > 0 ? wasmModule._malloc(payload.length) : 0;
  const outputPtr = wasmModule._malloc(payload.length + 256); // Room for header
  const outLenPtr = wasmModule._malloc(4); // 32-bit size_t

  if (!outputPtr || !outLenPtr || (payload.length > 0 && !payloadPtr)) {
    if (payloadPtr) wasmModule._free(payloadPtr);
    if (outputPtr) wasmModule._free(outputPtr);
    if (outLenPtr) wasmModule._free(outLenPtr);
    throw new Error('Failed to allocate memory for serialization');
  }

  try {
    // Copy payload to WASM memory
    if (payload.length > 0 && payloadPtr) {
      wasmModule.HEAPU8.set(payload, payloadPtr);
    }

    // Call serialization
    const result = wasmModule._client_serialize_packet(
      packetType,
      payloadPtr,
      payload.length,
      clientId,
      outputPtr,
      outLenPtr
    );

    if (result !== 0) {
      throw new Error('Serialization failed');
    }

    // Read output length
    const outLen = wasmModule.HEAP32[outLenPtr >> 2];

    // Copy serialized packet from WASM memory
    const packet = new Uint8Array(outLen);
    packet.set(wasmModule.HEAPU8.subarray(outputPtr, outputPtr + outLen));

    return packet;
  } finally {
    if (payloadPtr) wasmModule._free(payloadPtr);
    wasmModule._free(outputPtr);
    wasmModule._free(outLenPtr);
  }
}

/**
 * Send a video frame (placeholder for now)
 */
export async function sendVideoFrame(rgbaData: Uint8Array, width: number, height: number): Promise<void> {
  if (!wasmModule) {
    throw new Error('WASM module not initialized');
  }

  const dataPtr = wasmModule._malloc(rgbaData.length);
  if (!dataPtr) {
    throw new Error('Failed to allocate memory for video frame');
  }

  try {
    wasmModule.HEAPU8.set(rgbaData, dataPtr);
    const result = wasmModule._client_send_video_frame(dataPtr, width, height);
    if (result !== 0) {
      throw new Error('Failed to send video frame');
    }
  } finally {
    wasmModule._free(dataPtr);
  }
}

/**
 * Get current connection state
 */
export function getConnectionState(): ConnectionState {
  if (!wasmModule) {
    throw new Error('WASM module not initialized');
  }
  return wasmModule._client_get_connection_state();
}

/**
 * Check if WASM module is ready
 */
export function isWasmReady(): boolean {
  return wasmModule !== null && wasmModule.HEAPU8 !== undefined;
}
