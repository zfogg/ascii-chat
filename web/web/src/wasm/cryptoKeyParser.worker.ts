// @ts-expect-error Generated Emscripten factory does not ship TypeScript types.
import ClientModuleFactory from "./dist/client.js";

interface ParserModule {
  HEAPU8: Uint8Array;
  _malloc(size: number): number;
  _free(pointer: number): void;
  _client_parse_ssh_private_key(path: number, output: number): number;
  _client_parse_ssh_public_key(line: number, output: number): number;
  _client_parse_gpg_private_key(armoredKey: number, output: number): number;
  _client_parse_gpg_public_key(armoredKey: number, output: number): number;
  _client_parse_gpg_private_key_binary(
    data: number,
    length: number,
    output: number,
  ): number;
  _client_parse_gpg_public_key_binary(
    data: number,
    length: number,
    output: number,
  ): number;
  _client_get_crypto_error_message(output: number, size: number): number;
  lengthBytesUTF8(value: string): number;
  stringToUTF8(value: string, pointer: number, maximum: number): void;
  FS: {
    writeFile(path: string, contents: string, options: { mode: number }): void;
    unlink(path: string): void;
  };
}

let modulePromise: Promise<ParserModule> | null = null;
const BINARY_OPENPGP_KEY_PREFIX = "ascii-chat:openpgp-binary-base64:";
const postMessageToWorker = self.postMessage as unknown as (
  message: unknown,
  transfer?: Transferable[],
) => void;

async function getParserModule(): Promise<ParserModule> {
  modulePromise ??= ClientModuleFactory({
    locateFile: (path: string) =>
      new URL(`/wasm/${path}`, self.location.origin).href,
    getRandomValue: () => {
      const value = new Uint32Array(1);
      crypto.getRandomValues(value);
      return value[0];
    },
  }) as Promise<ParserModule>;
  return modulePromise;
}

function allocateUtf8(module: ParserModule, value: string): number {
  const size = module.lengthBytesUTF8(value) + 1;
  const pointer = module._malloc(size);
  if (!pointer) throw new Error("Unable to allocate SSH key input");
  module.stringToUTF8(value, pointer, size);
  return pointer;
}

function getParserError(module: ParserModule): string {
  const buffer = module._malloc(1024);
  if (!buffer) return "Invalid SSH key";
  try {
    module.HEAPU8.fill(0, buffer, buffer + 1024);
    if (module._client_get_crypto_error_message(buffer, 1024) !== 0)
      return "Invalid SSH key";
    const bytes = module.HEAPU8.subarray(buffer, buffer + 1024);
    const end = bytes.indexOf(0);
    const message = new Uint8Array(end >= 0 ? end : bytes.length);
    message.set(end >= 0 ? bytes.subarray(0, end) : bytes);
    return new TextDecoder().decode(message);
  } finally {
    module.HEAPU8.fill(0, buffer, buffer + 1024);
    module._free(buffer);
  }
}

self.onmessage = async (
  event: MessageEvent<{
    id: number;
    kind: "ssh-private" | "ssh-public" | "gpg-private" | "gpg-public";
    contents: string;
  }>,
) => {
  const { id, kind, contents } = event.data;
  let input = 0;
  let output = 0;
  let path = "";
  let inputByteLength = 0;
  try {
    const module = await getParserModule();
    if (kind === "ssh-private") {
      path = `/tmp/ascii-chat-key-${crypto.randomUUID()}`;
      input = allocateUtf8(module, path);
      output = module._malloc(64);
      if (!output) throw new Error("Unable to allocate SSH key output buffer");
      module.FS.writeFile(path, contents, { mode: 0o600 });
      if (module._client_parse_ssh_private_key(input, output) !== 0)
        throw new Error(getParserError(module));
      const bytes = new Uint8Array(64);
      bytes.set(module.HEAPU8.subarray(output, output + 64));
      postMessageToWorker({ id, bytes: bytes.buffer }, [bytes.buffer]);
    } else if (kind === "ssh-public") {
      input = allocateUtf8(module, contents);
      output = module._malloc(32);
      if (!output) throw new Error("Unable to allocate SSH key output buffer");
      if (module._client_parse_ssh_public_key(input, output) !== 0)
        throw new Error(getParserError(module));
      const bytes = new Uint8Array(32);
      bytes.set(module.HEAPU8.subarray(output, output + 32));
      postMessageToWorker({ id, bytes: bytes.buffer }, [bytes.buffer]);
    } else {
      const byteCount = kind === "gpg-private" ? 64 : 32;
      output = module._malloc(byteCount);
      if (!output)
        throw new Error("Unable to allocate OpenPGP key output buffer");
      let result: number;
      if (contents.startsWith(BINARY_OPENPGP_KEY_PREFIX)) {
        const binary = atob(contents.slice(BINARY_OPENPGP_KEY_PREFIX.length));
        if (!binary.length || binary.length > 1024 * 1024)
          throw new Error(
            "Binary OpenPGP key file must be between 1 byte and 1 MiB",
          );
        const bytes = Uint8Array.from(binary, (character) =>
          character.charCodeAt(0),
        );
        inputByteLength = bytes.length;
        input = module._malloc(inputByteLength);
        if (!input)
          throw new Error("Unable to allocate binary OpenPGP key input");
        module.HEAPU8.set(bytes, input);
        result =
          kind === "gpg-private"
            ? module._client_parse_gpg_private_key_binary(
                input,
                inputByteLength,
                output,
              )
            : module._client_parse_gpg_public_key_binary(
                input,
                inputByteLength,
                output,
              );
        bytes.fill(0);
      } else {
        input = allocateUtf8(module, contents);
        inputByteLength = module.lengthBytesUTF8(contents) + 1;
        result =
          kind === "gpg-private"
            ? module._client_parse_gpg_private_key(input, output)
            : module._client_parse_gpg_public_key(input, output);
      }
      if (result !== 0) throw new Error(getParserError(module));
      const bytes = new Uint8Array(byteCount);
      bytes.set(module.HEAPU8.subarray(output, output + byteCount));
      postMessageToWorker({ id, bytes: bytes.buffer }, [bytes.buffer]);
    }
  } catch (error) {
    postMessageToWorker({
      id,
      error: error instanceof Error ? error.message : String(error),
    });
  } finally {
    const module = modulePromise ? await modulePromise.catch(() => null) : null;
    if (module && output) {
      const byteCount = kind.endsWith("private") ? 64 : 32;
      module.HEAPU8.fill(0, output, output + byteCount);
      module._free(output);
    }
    if (module && input) {
      const length =
        inputByteLength || module.lengthBytesUTF8(path || contents) + 1;
      module.HEAPU8.fill(0, input, input + length);
      module._free(input);
    }
    if (module && path) {
      try {
        module.FS.unlink(path);
      } catch {
        // The parser can reject before opening the key file.
      }
    }
  }
};
