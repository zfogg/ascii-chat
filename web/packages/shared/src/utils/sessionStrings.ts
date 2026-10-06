import {
  adjectives,
  adjectives_count,
} from "./sessionStringAdjectives.js";
import { nouns, nouns_count } from "./sessionStringNouns.js";

const UINT32_RANGE = 0x1_0000_0000;

/**
 * Select a cryptographically random array index without modulo bias.
 *
 * This mirrors libsodium's randombytes_uniform() used by ascii-chat-strings,
 * while using the Web Crypto API available in browsers and Vercel Functions.
 */
function randomIndex(length: number): number {
  const limit = UINT32_RANGE - (UINT32_RANGE % length);
  const value = new Uint32Array(1);

  do {
    globalThis.crypto.getRandomValues(value);
  } while (value[0]! >= limit);

  return value[0]! % length;
}

/** Generate a memorable session string in the format adjective-noun-noun. */
export function generateSessionString(): string {
  return `${adjectives[randomIndex(adjectives_count)]}-${nouns[randomIndex(
    nouns_count,
  )]}-${nouns[randomIndex(nouns_count)]}`;
}

/** Generate the requested number of independent session strings. */
export function generateSessionStrings(count: number): string[] {
  return Array.from({ length: count }, generateSessionString);
}
