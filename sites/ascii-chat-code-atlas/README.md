# ascii-chat Code Atlas

An explorable dark source map of native C, browser TypeScript, and their shared C/WASM implementations. Open `ascii-chat-code-atlas.html` directly in a browser; it includes its data, styles, and code and needs no server or dependencies.

Features: 17 guided paths with 51 focused chapters and 153 source checkpoints, pan/zoom/fit, expanded canvas, hover context, search across 562 source files and 5,043 indexed symbols, immediate dependency exploration, C function traces, source excerpts, and commit-specific GitHub links. Select an arrow for exact source evidence. `src/` is the starting point; `lib/` and the web application's shared TypeScript package provide the adjacent implementations.

To serve the normal static site from this folder:

```powershell
python -m http.server 4178 --bind 127.0.0.1 --directory dist
```

To refresh the snapshot from the current repository working tree and rebuild the standalone copy:

```powershell
npm ci --ignore-scripts
python generate.py
python package.py
```

The snapshot records repository HEAD and includes current working-tree source. Generated WASM artifacts, headers, tests, and third-party dependencies are excluded. The source graph uses lexical references to uniquely indexed externally visible functions, relative module/resource references, mode entry points, and callback registrations. It is not a compiler call graph: build variants, macros, aliases, barrel exports, macros, methods, callbacks, and dynamic dispatch may be incomplete. C function traces filter cross-file evidence to the function body; they do not enumerate same-file calls. GitHub links show the recorded commit and can differ from an uncommitted working-tree excerpt.

The hosting-ready static files are in `dist/`. Sites registration and publication were blocked because this session did not expose the Sites create/save/deploy tools. No Site identity has been fabricated or registered. Existing repository changes were left intact.

Code excerpts and connection evidence use Shiki with the explicit `github-dark` theme and C, C++, Objective-C, JavaScript, TypeScript, and TSX grammars. Highlighting runs during packaging; both the static site and standalone HTML include the rendered tokens, with no runtime CDN dependency. See https://shiki.style/guide/install for the theme API.

Selected symbols show their complete declaration or function body, with no neighboring declarations or fixed line window. Connection evidence shows the complete referenced call or import. C uses balanced delimiters; TypeScript and TSX use the TypeScript compiler parser. Code blocks grow to fit their content and the inspector scrolls.

Guides are organized into Architecture, Make a call, Inside a call, and Control & recovery. Each guide has a user question, scope, outcome, three chapter diagrams, source checkpoints, failure boundaries, and related journeys. Function nodes inspect the complete selected declaration; Guide notes restores the walkthrough. Guide metadata lives in guides.py, guides_calls.py, guides_media.py, and guides_controls.py. validate_guides.py checks the requested 5+12 guide count and every primary function/file against the parsed snapshot.

The inspector is 1.7 times its previous desktop width: 527 px normally, 595 px at the large-screen breakpoint, and 442 px at the smaller desktop breakpoint. Mobile retains a viewport-fitting layout.
