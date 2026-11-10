# Simple renderer example

This example imports the published `ascii-chat` package, generates animated
test-pattern pixels, and rasterizes the shared `AsciiCanvas` output to an HTML
canvas.

```sh
npm install
npm run dev
```

The Vite config serves the package's WASM assets and enables the cross-origin
isolation required by the package's pthread-enabled WASM module.
