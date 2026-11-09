import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { resolve, extname, sep } from "node:path";
const root = resolve(process.env.ASCII_CHAT_TEST_PACKAGE_ROOT ?? "dist");
createServer(async (req, res) => {
  res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
  res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
  const path = resolve(
    root,
    "." + new URL(req.url, "http://localhost").pathname,
  );
  if (path !== root && !path.startsWith(root + sep)) {
    res.writeHead(403).end();
    return;
  }
  if (path === root) {
    res.setHeader("Content-Type", "text/html");
    res.end("<!doctype html><canvas></canvas>");
    return;
  }
  try {
    res.setHeader(
      "Content-Type",
      extname(path) === ".wasm" ? "application/wasm" : "text/javascript",
    );
    res.end(await readFile(path));
  } catch {
    res.writeHead(404).end();
  }
}).listen(4178, "127.0.0.1");
