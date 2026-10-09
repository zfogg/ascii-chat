// @ts-expect-error Generated Emscripten factory does not ship TypeScript types.
import ClientModuleFactory from "./dist/client.js";

interface ParserModule {
  HEAPU8: Uint8Array;
  _malloc(size: number): number;
  _free(pointer: number): void;
  _client_parse_ssh_private_key(path: number, output: number): number;
  _client_parse_ssh_public_key(line: number, output: number): number;
  _client_get_crypto_error_message(output: number, size: number): number;
  lengthBytesUTF8(value: string): number;
  stringToUTF8(value: string, pointer: number, maximum: number): void;
  FS: {
    writeFile(path: string, contents: string, options: { mode: number }): void;
    unlink(path: string): void;
  };
}

let modulePromise: Promise<ParserModule> | null = null;
const postMessageToWorker = self.postMessage as unknown as (
  message: unknown,
  transfer?: Transferable[],
) => void;

async function getParserModule(): Promise<ParserModule> {
  modulePromise ??= ClientModuleFactory({
    locateFile: (path: string) => new URL(`/wasm/${path}`, self.location.origin).href,
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
  event: MessageEvent<{ id: number; kind: "private" | "public"; contents: string }>,
) => {
  const { id, kind, contents } = event.data;
  let input = 0;
  let output = 0;
  let path = "";
  try {
    const module = await getParserModule();
    if (kind === "private") {
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
    } else {
      input = allocateUtf8(module, contents);
      output = module._malloc(32);
      if (!output) throw new Error("Unable to allocate SSH key output buffer");
      if (module._client_parse_ssh_public_key(input, output) !== 0)
        throw new Error(getParserError(module));
      const bytes = new Uint8Array(32);
      bytes.set(module.HEAPU8.subarray(output, output + 32));
      postMessageToWorker({ id, bytes: bytes.buffer }, [bytes.buffer]);
    }
  } catch (error) {
    postMessageToWorker({ id, error: error instanceof Error ? error.message : String(error) });
  } finally {
    const module = modulePromise ? await modulePromise.catch(() => null) : null;
    if (module && output) {
      const byteCount = kind === "private" ? 64 : 32;
      module.HEAPU8.fill(0, output, output + byteCount);
      module._free(output);
    }
    if (module && input) {
      module.HEAPU8.fill(0, input, input + module.lengthBytesUTF8(path || contents) + 1);
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
