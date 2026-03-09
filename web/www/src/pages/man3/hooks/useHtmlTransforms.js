import { useCallback } from "react";
import { highlightMatchesInHTML } from "../utils/highlight.jsx";

/**
 * Hook for all HTML string transformation functions
 * Used in the preprocessing pipeline to convert raw Doxygen HTML to clean documentation
 *
 * @param {React.MutableRefObject<Set>} validPagesRef - Set of valid page names for link detection
 * @returns {{ processPageContent, preprocessPageHTML, processDefinitionLinks }}
 */
export function useHtmlTransforms(validPagesRef) {
  /**
   * Parse a macro match and convert to HTML table
   */
  const parseMacroMatch = useCallback((match) => {
    const content = match.replace(/<p[^>]*>/, "").replace(/<\/p>/, "");
    const sections = content.split(/<br\s*\/?>/i);
    if (sections.length < 1) return null;

    const rows = [];
    let previousMacro = null;

    for (let i = 0; i < sections.length; i++) {
      const section = sections[i].trim();
      if (!section) continue;

      // Extract bold tags and text
      const boldRegex = /<b>([^<]+)<\/b>/g;
      const bolds = [];
      let boldMatch;
      while ((boldMatch = boldRegex.exec(section))) {
        bolds.push(boldMatch[1]);
      }

      if (bolds.length === 0) continue;

      let name,
        value,
        description = "";

      if (bolds.length >= 1) {
        name = bolds[0];

        if (bolds.length >= 2) {
          value = bolds[1];
        } else {
          // Extract value from text
          const text = section.replace(/<[^>]+>/g, "");
          const nameIndex = text.indexOf(name);
          if (nameIndex !== -1) {
            const afterName = text.substring(nameIndex + name.length).trim();
            const closeParen = afterName.indexOf(")");
            if (closeParen !== -1) {
              value = afterName.substring(0, closeParen + 1);
            } else {
              const firstSpace = afterName.indexOf(" ");
              if (firstSpace !== -1) {
                value = afterName.substring(0, firstSpace);
              } else {
                value = afterName;
              }
            }
          }
        }

        // Extract description
        let plainText = section.replace(/<[^>]+>/g, "").trim();
        plainText = plainText
          .replace(
            new RegExp(
              `^#define\\s+${name.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}\\s*${(
                value || ""
              ).replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`,
            ),
            "",
          )
          .trim();

        // Remove any subsequent #define statements from the description
        const nextDefineIndex = plainText.indexOf("#define");
        if (nextDefineIndex !== -1) {
          plainText = plainText.substring(0, nextDefineIndex).trim();
        }

        description = plainText;

        if (previousMacro && !previousMacro.description) {
          previousMacro.description = description;
          rows.push(previousMacro);
          previousMacro = null;
          description = "";
        }
      }

      if (name) {
        previousMacro = { name, value: value || "", description };
      }
    }

    if (previousMacro) {
      rows.push(previousMacro);
    }

    if (rows.length === 0) return null;

    // Build table HTML
    let tableHtml = '<table class="man-macros-table"><tbody>';
    for (const row of rows) {
      const nameCell = row.name ? `<code>${row.name}</code>` : "";
      const valueCell = row.value ? `<code>${row.value}</code>` : "";
      tableHtml += `<tr><td class="man-macro-name">${nameCell}</td><td class="man-macro-value">${valueCell}</td><td class="man-macro-desc">${row.description}</td></tr>`;
    }
    tableHtml += "</tbody></table>";

    return tableHtml;
  }, []);

  /**
   * Transform macro P tags into tables
   */
  const transformMacrosInHTML = useCallback(
    (html) => {
      // Pattern 1: Standard sections that start with <br> (Password Requirements, etc.)
      let result = html.replace(
        /<p[^>]*class="Pp"[^>]*>\s*<br[^>]*>[\s\S]*?#define[\s\S]*?<\/p>/g,
        (match) => parseMacroMatch(match) || match,
      );

      // Pattern 2: Initial Macros section (starts directly with #define, allowing optional whitespace)
      // Only match if it contains #define statements
      result = result.replace(
        /<p[^>]*class="Pp"[^>]*>\s*#define[\s\S]*?<\/p>/g,
        (match) => {
          // Skip if this is just a header (contains only bold text without #define)
          if (!match.includes("#define")) return match;
          return parseMacroMatch(match) || match;
        },
      );

      return result;
    },
    [parseMacroMatch],
  );

  /**
   * Transform filename references to man3 links
   */
  const transformFilenameLinksInHTML = useCallback(
    (html) => {
      const fileRegex = /\b([a-zA-Z0-9_\-./]+\.(c|h|cpp|m|hpp))\b/g;

      return html.replace(fileRegex, (match, filename) => {
        // Check if already inside a link
        const beforeMatch = html.substring(0, html.indexOf(match));
        const openLinks = (beforeMatch.match(/<a[^>]*>/g) || []).length;
        const closeLinks = (beforeMatch.match(/<\/a>/g) || []).length;

        if (openLinks > closeLinks) {
          // Already inside a link
          return match;
        }

        // Check if file exists in pages
        if (validPagesRef.current.has(filename)) {
          return `<a href="/man3?page=${filename}" class="text-cyan-400 hover:text-cyan-300 underline">${filename}</a>`;
        }
        return match;
      });
    },
    [validPagesRef],
  );

  /**
   * Add GitHub links to "Definition at line X of file Y"
   * Also converts line number anchors on source pages to GitHub links
   */
  const processDefinitionLinks = useCallback(
    (html, sourcePath, commitSha, isSourcePage = false) => {
      if (!sourcePath || !commitSha || commitSha === "unknown") return html;

      // Transform "Definition at line X of file Y" text to add GitHub links with commit hash
      // Use \s* to handle whitespace/newlines between "of file" and the filename
      let result = html.replace(
        /Definition at line <b>(\d+)<\/b> of file\s*<b>([^<]+)<\/b>/g,
        (_, lineNum, filename) =>
          `<a href="https://github.com/zfogg/ascii-chat/blob/${commitSha}/${sourcePath}#L${lineNum}" ` +
          `target="_blank" rel="noopener noreferrer" class="text-cyan-400 hover:text-cyan-300">` +
          `Definition at line <b>${lineNum}</b> of file <b>${filename}</b></a>`,
      );

      // On source pages, transform line number anchors to GitHub links
      // e.g., <a id="l00022">22</a> becomes <a href="...#L22" ...>22</a>
      if (isSourcePage) {
        result = result.replace(
          /<a\s+id="l(\d+)"[^>]*>(\d+)<\/a>/g,
          (_, lineNum, displayNum) =>
            `<a id="l${lineNum}" href="https://github.com/zfogg/ascii-chat/blob/${commitSha}/${sourcePath}#L${lineNum}" ` +
            `target="_blank" rel="noopener noreferrer" class="text-cyan-400 hover:text-cyan-300">${displayNum}</a>`,
        );
      }

      return result;
    },
    [],
  );

  /**
   * Transform function P tags into tables
   */
  const transformFunctionsInHTML = (html) => {
    // First, remove __attribute__ tags to clean up the HTML for regex matching
    let cleaned = html.replace(
      /<b>__attribute__<\/b>\s*\(\([^)]*\)\)+\s*/g,
      "",
    );

    // Find ALL P tags first, then check if they contain function signatures
    const result = cleaned.replace(
      /<p\s[^>]*>([\s\S]*?)<\/p>/g,
      (match, content) => {
        // Skip P tags with macro definitions - let them render as plain text to preserve grouping
        if (match.includes("#define")) {
          return match;
        }

        // Check if this P tag contains a function signature (bold name followed by paren)
        if (!/<b>[a-zA-Z_][a-zA-Z0-9_]*<\/b>\s*\(/.test(content)) {
          return match;
        }

        // Remove __attribute__ directives and visibility markers from content (various formats)
        content = content.replace(
          /__attribute__\s*\(\(\s*visibility\s*\(\s*['"]*\w+['"]*\s*\)\s*\)\)\s*/g,
          "",
        );
        content = content.replace(/__attribute__\s*\(\([^)]*\)\)\s*/g, "");
        content = content.replace(/__attribute__\s*\([^)]*\)\s*/g, "");

        // Extract plain text for the table
        const tempDiv = document.createElement("div");
        tempDiv.innerHTML = content;
        const fullPlainText = tempDiv.textContent.trim();

        // Extract function name from the regex match
        const funcMatch = content.match(
          /<b>([a-zA-Z_][a-zA-Z0-9_]*)<\/b>\s*\(/,
        );
        if (funcMatch) {
          const funcName = funcMatch[1];

          // Extract function signature (from start through closing paren)
          const nameIdx = fullPlainText.indexOf(funcName);
          const openParenIdx = fullPlainText.indexOf("(", nameIdx);

          // Count parentheses to find the matching closing paren
          let parenCount = 1; // We already have one opening paren
          let closeParenIdx = -1;
          for (let i = openParenIdx + 1; i < fullPlainText.length; i++) {
            if (fullPlainText[i] === "(") parenCount++;
            if (fullPlainText[i] === ")") {
              parenCount--;
              if (parenCount === 0) {
                closeParenIdx = i;
                break;
              }
            }
          }

          // If no closing paren found, find the first line break or description
          if (closeParenIdx === -1) {
            // Look for the first newline followed by non-whitespace and non-opening bracket
            const match = fullPlainText
              .substring(openParenIdx)
              .match(/\n\s*([a-zA-Z])/);
            if (match) {
              closeParenIdx = openParenIdx + match.index;
            }
          }

          // Extract the full signature (from start through closing paren, or to first description)
          let signature = "";
          let description = "";

          if (closeParenIdx !== -1) {
            // We found a paren, use it
            signature = fullPlainText.substring(0, closeParenIdx + 1).trim();
            description = fullPlainText.substring(closeParenIdx + 1).trim();
          } else {
            // No paren found, split at first newline pattern that looks like description start
            const lines = fullPlainText.split("\n");
            let sigLines = [];
            let descLines = [];
            let foundDesc = false;

            for (let i = 0; i < lines.length; i++) {
              const line = lines[i].trim();
              if (!foundDesc && line.length > 0 && !line.includes("(")) {
                // This looks like the start of description
                foundDesc = true;
              }
              if (!foundDesc) {
                sigLines.push(lines[i]);
              } else {
                descLines.push(line);
              }
            }

            signature = sigLines.join("\n").trim();
            description = descLines.join(" ").trim();
          }

          // Build table with name, full signature, and description
          let tableHtml = '<table class="man-functions-table"><tbody>';
          tableHtml += `<tr><td class="man-func-name"><code>${funcName}</code></td><td class="man-func-sig"><code>${signature}</code></td><td class="man-func-desc">${description}</td></tr>`;
          tableHtml += "</tbody></table>";
          return tableHtml;
        }

        // Fallback: if we couldn't extract function, return original match
        return match;
      },
    );

    return result;
  };

  /**
   * Comprehensive HTML preprocessing pipeline - applies ALL transformations
   */
  const preprocessPageHTML = useCallback(
    (
      html,
      searchQuery = "",
      sourcePath = "",
      commitSha = "",
      isSourcePage = false,
    ) => {
      if (!html) return html;

      // Step 1: Transform macros to tables
      let processed = transformMacrosInHTML(html);

      // Step 1b: Transform functions to tables
      processed = transformFunctionsInHTML(processed);

      // Step 2: Fix relative paths
      processed = processed.replace(/src="([^"]+)"/g, (match, src) => {
        if (!src.startsWith("/") && !src.startsWith("http")) {
          return `src="/man3/${src}"`;
        }
        return match;
      });
      processed = processed.replace(/href="([^"]+)"/g, (match, href) => {
        if (
          !href.startsWith("/") &&
          !href.startsWith("http") &&
          !href.startsWith("#")
        ) {
          return `href="/man3/${href}"`;
        }
        return match;
      });

      // Step 3: Convert HTML file links to man3 links
      processed = processed.replace(
        /href="(?:https?:\/\/[^/]+)?([^"]*\/)?([^/".]+\.html)(#l\d+)?"/gi,
        (match, path, htmlFile, anchor) => {
          const newPageName = htmlFile.replace(".html", "");
          const newHref = `/man3?page=${newPageName}${anchor || ""}`;
          return `href="${newHref}"`;
        },
      );

      // Step 4: Preserve empty anchor tags
      processed = processed.replace(
        /<a\s+id="(l\d+)"[^>]*>\s*<\/a>/g,
        '<a id="$1">\u200B</a>',
      );

      // Step 5: Add GitHub links to definition references
      processed = processDefinitionLinks(
        processed,
        sourcePath,
        commitSha,
        isSourcePage,
      );

      // Step 6: Transform filenames to man3 links (in HTML)
      processed = transformFilenameLinksInHTML(processed);

      // Step 7: Highlight search matches
      processed = highlightMatchesInHTML(processed, searchQuery);

      return processed;
    },
    [
      processDefinitionLinks,
      transformMacrosInHTML,
      transformFilenameLinksInHTML,
    ],
  );

  /**
   * Process HTML content: extract body and apply preprocessing pipeline
   */
  const processPageContent = useCallback(
    (html, searchQuery) => {
      const bodyMatch = html.match(/<body[^>]*>([\s\S]*)<\/body>/i);
      let content = bodyMatch ? bodyMatch[1] : html;

      // Apply comprehensive preprocessing pipeline
      return preprocessPageHTML(content, searchQuery);
    },
    [preprocessPageHTML],
  );

  return {
    processPageContent,
    preprocessPageHTML,
    processDefinitionLinks,
  };
}
