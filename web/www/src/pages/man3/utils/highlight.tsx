/**
 * Shared utility for search highlighting logic
 */

/**
 * Build a search regex from user query
 * Supports two formats:
 * 1. /pattern/flags - explicit regex with flags
 * 2. plain text - literal string search (case-insensitive)
 *
 * @param {string} query - The search query
 * @returns {{ regex: RegExp | null, isValid: boolean }}
 */
export function buildSearchRegex(query: string) {
  if (!query?.trim()) {
    return { regex: null, isValid: true };
  }

  try {
    let regex;
    const regexMatch = query.match(/^\/(.+)\/([gimuy]*)$/);

    if (regexMatch) {
      // Parse /pattern/flags format
      const pattern = regexMatch[1];
      const flags = (regexMatch[2] || "i").includes("g")
        ? regexMatch[2]
        : (regexMatch[2] || "i") + "g";
      regex = new RegExp(`(${pattern})`, flags);
    } else {
      // Literal string - escape special chars
      const escaped = query.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
      regex = new RegExp(`(${escaped})`, "gi");
    }

    return { regex, isValid: true };
  } catch (_e) {
    return { regex: null, isValid: false };
  }
}

/**
 * Highlight text matches in JSX (for left panel snippet rendering)
 * Returns an array of React elements with matches wrapped in highlighted spans
 *
 * @param {string} text - The text to highlight
 * @param {string} query - The search query
 * @returns {JSX.Element[] | string} - Array of spans or original text if no query
 */
export function highlightMatches(text: string, query: string) {
  if (!query?.trim()) return text;

  const { regex, isValid } = buildSearchRegex(query);
  if (!isValid || !regex) return text;

  try {
    const parts = text.split(regex);

    return parts.map((part: string, i: number) => {
      // Every other part (odd indices) is the matched text
      if (i % 2 === 1) {
        return (
          <span key={i} className="bg-yellow-900/50 text-yellow-200">
            {part}
          </span>
        );
      }
      return <span key={i}>{part}</span>;
    });
  } catch (_e) {
    return text;
  }
}

/**
 * Highlight text matches in HTML string (for content viewer preprocessing)
 * Returns HTML string with matches wrapped in highlight spans
 *
 * @param {string} html - The HTML to highlight
 * @param {string} query - The search query
 * @returns {string} - HTML string with highlights added
 */
export function highlightMatchesInHTML(html: string, query: string) {
  if (!query?.trim()) return html;

  const { regex, isValid } = buildSearchRegex(query);
  if (!isValid || !regex) return html;

  try {
    // For code blocks: preserve Doxygen syntax highlighting while adding search highlight
    // Wrap matches with highlight span but preserve existing span classes
    return html.replace(
      regex,
      '<span class="bg-yellow-900/50 text-yellow-200">$1</span>',
    );
  } catch (_e) {
    return html;
  }
}
