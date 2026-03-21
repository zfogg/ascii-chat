import { adjectives, adjectives_count } from "./adjectives.ts";
import { nouns, nouns_count } from "./nouns.ts";

/**
 * Generate cryptographically random bytes uniformly distributed within a range.
 * Uses crypto.getRandomValues for browser environments.
 */
function randombytes_uniform(upper_bound: number): number {
  if (upper_bound === 0) return 0;

  // Use crypto.getRandomValues if available (browser/Node.js)
  if (
    typeof globalThis.crypto !== "undefined" &&
    globalThis.crypto.getRandomValues
  ) {
    const array = new Uint32Array(1);
    globalThis.crypto.getRandomValues(array);
    return array[0]! % upper_bound;
  }

  // Fallback for environments without crypto
  return Math.floor(Math.random() * upper_bound);
}

/**
 * Generate a single memorable session string in the format: adjective-noun-noun
 */
export function generateSessionString(): string {
  const adj_idx = randombytes_uniform(adjectives_count);
  const noun1_idx = randombytes_uniform(nouns_count);
  const noun2_idx = randombytes_uniform(nouns_count);

  return `${adjectives[adj_idx]}-${nouns[noun1_idx]}-${nouns[noun2_idx]}`;
}

/**
 * Generate multiple session strings
 */
export function generateSessionStrings(count: number): string[] {
  const strings = [];
  for (let i = 0; i < count; i++) {
    strings.push(generateSessionString());
  }
  return strings;
}
