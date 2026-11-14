#pragma once

/** Capability bits owned by ascii-chat's terminal-rendering protocol. */
enum {
  TERM_CAP_GLYPH_RENDERER = 0x0040,
  TERM_CAP_EXPLICIT_PALETTE = 0x0080,
  TERM_CAP_STABILIZE = 0x0100,
};
