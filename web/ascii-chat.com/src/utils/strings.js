import { adjectives, adjectives_count } from "./adjectives.js";
import { nouns, nouns_count } from "./nouns.js";

/**
 * Generate cryptographically random bytes uniformly distributed within a range.
 * Uses crypto.getRandomValues for browser environments.
 *
 * @param {number} upper_bound - Maximum value (exclusive)
 * @returns {number} Random value in [0, upper_bound)
 */
function randombytes_uniform(upper_bound) {
  if (upper_bound === 0) return 0;

  // Use crypto.getRandomValues if available (browser/Node.js)
  if (typeof globalThis.crypto !== "undefined" && globalThis.crypto.getRandomValues) {
    const array = new Uint32Array(1);
    globalThis.crypto.getRandomValues(array);
    return array[0] % upper_bound;
  }

  // Fallback for environments without crypto
  return Math.floor(Math.random() * upper_bound);
}

/**
 * Generate a single memorable session string in the format: adjective-noun-noun
 *
 * @returns {string} A session string like "swift-river-mountain"
 */
export function generateSessionString() {
  const adj_idx = randombytes_uniform(adjectives_count);
  const noun1_idx = randombytes_uniform(nouns_count);
  const noun2_idx = randombytes_uniform(nouns_count);

  return `${adjectives[adj_idx]}-${nouns[noun1_idx]}-${nouns[noun2_idx]}`;
}

/**
 * Generate multiple session strings
 *
 * @param {number} count - Number of strings to generate
 * @returns {string[]} Array of session strings
 */
export function generateSessionStrings(count) {
  const strings = [];
  for (let i = 0; i < count; i++) {
    strings.push(generateSessionString());
  }
  return strings;
}
