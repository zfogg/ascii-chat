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
   * Handles multiple functions in a single P tag by splitting at function boundaries
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

        // Extract plain text for parsing
        const tempDiv = document.createElement("div");
        tempDiv.innerHTML = content;
        const fullPlainText = tempDiv.textContent.trim();

        // Find ALL function names in the content (bold text followed by paren)
        const funcNameMatches = [];
        const boldRegex = /<b>([a-zA-Z_][a-zA-Z0-9_]*)<\/b>\s*\(/g;
        let boldMatch;
        while ((boldMatch = boldRegex.exec(content))) {
          funcNameMatches.push(boldMatch[1]);
        }

        // If no functions found, return original
        if (funcNameMatches.length === 0) {
          return match;
        }

        // Split the full plain text into function entries
        // Use a regex to find all function signatures, handling wrapping
        const rows = [];

        // Find all function names by looking for patterns like "funcName ("
        const funcNamePattern = /([a-zA-Z_][a-zA-Z0-9_]*)\s*\(/g;
        const funcMatches = [];
        let fnMatch;
        while ((fnMatch = funcNamePattern.exec(fullPlainText))) {
          funcMatches.push({
            name: fnMatch[1],
            startIdx: fnMatch.index,
          });
        }

        // For each function match, extract the full signature and description
        for (let i = 0; i < funcMatches.length; i++) {
          const currentMatch = funcMatches[i];
          const nextMatch = funcMatches[i + 1];

          // Find where this function's content ends (start of next function or end of text)
          const contentEnd = nextMatch
            ? nextMatch.startIdx
            : fullPlainText.length;
          const functionContent = fullPlainText.substring(
            currentMatch.startIdx,
            contentEnd,
          );

          // Find the opening paren for this function
          const openParenIdx = functionContent.indexOf("(");
          let closeParenIdx = -1;
          let parenCount = 1;

          for (let j = openParenIdx + 1; j < functionContent.length; j++) {
            if (functionContent[j] === "(") parenCount++;
            if (functionContent[j] === ")") {
              parenCount--;
              if (parenCount === 0) {
                closeParenIdx = j;
                break;
              }
            }
          }

          // Extract signature and description
          let signature = "";
          let description = "";

          if (closeParenIdx !== -1) {
            // Get everything up to and including the closing paren as the params+name part
            const nameAndParams = functionContent
              .substring(0, closeParenIdx + 1)
              .trim();

            // Extract the return type from the text before the function name
            // Look backwards from currentMatch.startIdx to find the return type
            let returnType = "";
            if (currentMatch.startIdx > 0) {
              // Get text before the function name
              const beforeFunc = fullPlainText
                .substring(0, currentMatch.startIdx)
                .trim();
              // Return type is the last "word-like" token before the function name
              // It could be: void, char, char *, uint8_t, struct X, const char *, etc.
              const parts = beforeFunc.split(/\s+/);
              // Take the last part(s) that look like a return type
              if (parts.length > 0) {
                // Simple heuristic: last 1-2 parts are the return type (e.g., "char" or "char *")
                returnType = parts[parts.length - 1];
                // If it's just a pointer symbol or continuation, get the previous part too
                if (
                  parts.length > 1 &&
                  (returnType === "*" || returnType === "const")
                ) {
                  returnType = parts[parts.length - 2] + " " + returnType;
                }
              }
            }

            // Full signature: return_type name(params)
            signature = (returnType ? returnType + " " : "") + nameAndParams;

            // Everything after the closing paren is description (don't trim yet - regex needs whitespace)
            description = functionContent.substring(closeParenIdx + 1);

            // The description may be followed by the return type of the next function
            // Remove return type patterns that appear at the end of the description
            // Return types include: void, char *, int, bool, uint8_t, struct X, const char *, etc.
            // Pattern: optional whitespace + return type (including pointer types) + optional whitespace at end
            description = description
              .replace(
                /\s*(?:void|bool|int|char\s*\*?|size_t|uint\d+_t|struct\s+\w+(?:\s*\*)?|const\s+\w+(?:\s+\*)?|unsigned\s+\w+|static\s+\w+(?:\s*\*)?)\s*$/i,
                "",
              )
              .trim();
          } else {
            // No closing paren found, use whole content as signature
            signature = functionContent.trim();
            description = "";
          }

          // Remove the function name from the signature (it's already in the first column)
          // Replace "funcName (" with "("
          const signatureWithoutName = signature.replace(
            new RegExp(
              `\\b${currentMatch.name.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}\\s*\\(`,
            ),
            "(",
          );

          rows.push({
            name: currentMatch.name,
            signature: signatureWithoutName,
            description: description,
          });
        }

        // Build table with all rows
        let tableHtml = '<table class="man-functions-table"><tbody>';
        for (const row of rows) {
          tableHtml += `<tr><td class="man-func-name"><code>${row.name}</code></td><td class="man-func-sig"><code>${row.signature}</code></td><td class="man-func-desc">${row.description}</td></tr>`;
        }
        tableHtml += "</tbody></table>";
        return tableHtml;
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
