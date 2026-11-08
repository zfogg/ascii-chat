---
name: ascii-chat-documentation
description: Maintain ascii-chat's Doxygen topics, authored guides, navigation, and generated documentation.
metadata:
  short-description: ascii-chat documentation maintenance
---

# ascii-chat documentation

Keep documentation synchronized with the implementation. Before changing a
public API, subsystem, transport, option, screen, or workflow, inspect the
existing topic and authored guides and update or create the relevant page in
the same change. Prefer source-oriented `docs/topics/*.dox` pages for durable
architecture and API guidance; use Markdown for focused operational or
research records.

Keep navigation intentional: add durable subsystem pages to Topics and Topic
Categories, and avoid loose root-level pages that spam the Doxygen sidebar.

Verify with the docs-only project in `docs/CMakeLists.txt`:

```sh
cmake -S docs -B build/docs-standalone -DASCII_CHAT_SOURCE_DIR=$PWD
cmake --build build/docs-standalone --target docs
```

Check generated HTML for broken links, topic placement, and stale claims before
committing. Verify the hosted page after documentation deployment completes.
