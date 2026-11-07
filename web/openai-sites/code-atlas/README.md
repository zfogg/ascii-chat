# ascii-chat Code Atlas

An explorable dark source map of native C, browser TypeScript, and their shared C/WASM implementations. Open `ascii-chat-code-atlas.html` directly in a browser; it includes its data, styles, and code and needs no server or dependencies.

Features: 18 guided paths with 61 focused chapters and 183 source checkpoints, pan/zoom/fit, expanded canvas, hover context, search across 580 source files and 5,142 indexed symbols, immediate dependency exploration, C function traces, source excerpts, and commit-specific GitHub links. Select an arrow for exact source evidence. `src/` is the starting point; `lib/` and the web application's shared TypeScript package provide the adjacent implementations.

To serve the normal static site from this folder:

```powershell
python -m http.server 4178 --bind 127.0.0.1 --directory dist
```

To refresh the snapshot from the current repository working tree and rebuild the standalone copy:

```powershell
vp install --ignore-scripts
python generate.py
python package.py
python prepare_cloudflare.py
```

The snapshot records repository HEAD and includes current working-tree source. Generated WASM artifacts, headers, tests, and third-party dependencies are excluded. The source graph uses lexical references to uniquely indexed externally visible functions, relative module/resource references, mode entry points, and callback registrations. It is not a compiler call graph: build variants, macros, aliases, barrel exports, methods, callbacks, and dynamic dispatch may be incomplete. C function traces filter cross-file evidence to the function body; they do not enumerate same-file calls. GitHub links show the recorded commit and can differ from an uncommitted working-tree excerpt.

The source lives in `web/chatgpt-sites/ascii-chat-code-atlas/`. The public site is https://code-guide.ascii-chat.com/. The static HTML, CSS, and JavaScript in `dist/` are authored source and stay tracked. Generated `dist/data.json`, the standalone HTML bundle, root-level QA screenshots, dependency directories, and deployment output are ignored; rebuild them with the commands above after cloning.

Run `python prepare_cloudflare.py` after rebuilding to create `cloudflare-dist/`, which compresses the source index below Cloudflare's per-asset size limit and loads it with the browser's `DecompressionStream` API. The script verifies an exact compression round trip. Sites identity is recorded in the tracked `.openai/hosting.json`. Publication uses the compressed files as `dist/` in the ignored `.sites-checkout/` repository, keeping Site source synchronization separate from the main repository. Preserve the existing project ID and public audience when publishing with the Sites skill; never commit credentials or the nested publication checkout. Dependencies are managed by the web pnpm workspace and `web/pnpm-lock.yaml`; run Vite+ dependency commands from this directory or `web/`.

Code excerpts and connection evidence use Shiki with the explicit `github-dark` theme and C, C++, Objective-C, JavaScript, TypeScript, and TSX grammars. Highlighting runs during packaging; both the static site and standalone HTML include the rendered tokens, with no runtime CDN dependency. See https://shiki.style/guide/install for the theme API.

Selected symbols show their complete declaration or function body, with no neighboring declarations or fixed line window. Connection evidence shows the complete referenced call or import. C uses balanced delimiters; TypeScript and TSX use the TypeScript compiler parser. Code blocks grow to fit their content and the inspector scrolls.

Guides are organized into Architecture, Make a call, Inside a call, and Control & recovery. Each guide has a user question, scope, outcome, at least three chapter diagrams, source checkpoints, failure boundaries, and related journeys. Function nodes inspect the complete selected declaration; Guide notes restores the walkthrough. Guide metadata lives in guides.py, guides_calls.py, guides_media.py, guides_controls.py, and guides_audio.py. validate_guides.py checks all 18 guides and every primary function/file against the parsed snapshot and current source. Connection, receive, audio, and retry checkpoints follow the current connection owner, protocol adapter, and shared session protocol.

The inspector is 1.7 times its previous desktop width: 527 px normally, 595 px at the large-screen breakpoint, and 442 px at the smaller desktop breakpoint. Mobile retains a viewport-fitting layout.

Validation completed: all 17 overview routes and all 51 chapter views were opened in the in-app browser. Authored source checkpoints were checked against current parsed source; all chapter diagrams contain source-backed connections. Search-to-guide navigation and exact function inspection were also checked.

Full audio pipeline adds ten chapters and thirty source checkpoints covering source policy, FFmpeg PCM, device queues, AEC3, FFTW spectral DSP, Opus, host mixing, playback gain, browser audio, and recording. Long-guide overview maps select four chapter groups; every stage remains available through chapter tabs.
