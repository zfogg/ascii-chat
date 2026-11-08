import {
  createWasmModule,
  type ClientWasmBindings,
} from "@ascii-chat/shared/wasm";
import type { BrowserErrorFrame } from "../errors/errorBus";

const MAX_OPENPGP_KEY_FILE_BYTES = 1024 * 1024;
const ASCIICHAT_ERRNO_MAX_DEPTH = 64;
const BINARY_OPENPGP_KEY_PREFIX = "ascii-chat:openpgp-binary-base64:";

type ParserModule = Pick<
  ClientWasmBindings,
  "parseKey" | "keyParserError" | "errorSnapshot" | "errorFrame"
> & {
  FS: {
    writeFile(path: string, contents: string, options: { mode: number }): void;
    unlink(path: string): void;
  };
};

function getErrorFrames(module: ParserModule): BrowserErrorFrame[] {
  const count = Math.min(module.errorSnapshot(), ASCIICHAT_ERRNO_MAX_DEPTH);
  return Array.from({ length: count }, (_, index) => module.errorFrame(index));
}

let modulePromise: Promise<ParserModule> | null = null;
const postMessageToWorker = self.postMessage as unknown as (
  message: unknown,
  transfer?: Transferable[],
) => void;

async function getParserModule(): Promise<ParserModule> {
  modulePromise ??= createWasmModule<ParserModule>("client");
  return modulePromise;
}

self.onmessage = async (
  event: MessageEvent<{
    id: number;
    kind: "ssh-private" | "ssh-public" | "gpg-private" | "gpg-public";
    contents: string;
  }>,
) => {
  const { id, kind, contents } = event.data;
  let input: Uint8Array | undefined;
  let path = "";
  try {
    const module = await getParserModule();
    let binary = false;
    if (kind === "ssh-private") {
      path = `/tmp/ascii-chat-key-${crypto.randomUUID()}`;
      module.FS.writeFile(path, contents, { mode: 0o600 });
      input = new TextEncoder().encode(path);
    } else if (
      kind.startsWith("gpg-") &&
      contents.startsWith(BINARY_OPENPGP_KEY_PREFIX)
    ) {
      const encoded = atob(contents.slice(BINARY_OPENPGP_KEY_PREFIX.length));
      if (!encoded.length || encoded.length > MAX_OPENPGP_KEY_FILE_BYTES)
        throw new Error(
          "Binary OpenPGP key file must be between 1 byte and 1 MiB",
        );
      input = Uint8Array.from(encoded, (character) => character.charCodeAt(0));
      binary = true;
    } else {
      input = new TextEncoder().encode(contents);
    }
    const kinds = {
      "ssh-private": 0,
      "ssh-public": 1,
      "gpg-private": 2,
      "gpg-public": 3,
    } as const;
    const result = module.parseKey(kinds[kind], binary, input);
    if (result.code !== 0) throw new Error(module.keyParserError());
    const bytes = result.bytes;
    postMessageToWorker({ id, bytes: bytes.buffer }, [
      bytes.buffer as ArrayBuffer,
    ]);
  } catch (error) {
    const module = modulePromise ? await modulePromise.catch(() => null) : null;
    postMessageToWorker({
      id,
      error: error instanceof Error ? error.message : String(error),
      frames: module ? getErrorFrames(module) : [],
    });
  } finally {
    input?.fill(0);
    const module = modulePromise ? await modulePromise.catch(() => null) : null;
    if (module && path) {
      try {
        module.FS.unlink(path);
      } catch {
        /* The parser can reject before opening the key file. */
      }
    }
  }
};
