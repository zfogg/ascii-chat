import { useCallback } from "react";
import { highlightMatchesInHTML } from "../utils/highlight.jsx";

/**
 * Hook for all HTML string transformation functions
 * Used in the preprocessing pipeline to convert raw Doxygen HTML to clean documentation
 *
 * @param {React.MutableRefObject<Set>} validPagesRef - Set of valid page names for link detection
 * @param {Array} manPages - Full pages array with sourcePath information
 * @param {string} commitSha - Git commit SHA for GitHub links
 * @returns {{ processPageContent, preprocessPageHTML, processDefinitionLinks }}
 */
export function useHtmlTransforms(validPagesRef, manPages, commitSha) {
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
    (
      html,
      sourcePath,
      passedCommitSha,
      isSourcePage = false,
      selectedPageName = null,
    ) => {
      // Get the commit SHA - use passed value, then hook value, finally default to master
      const sha = passedCommitSha || commitSha;
      const githubRef = sha && sha !== "unknown" ? sha : "master";

      // Create fresh regex for each call to avoid state issues
      // Try multiple patterns to handle different HTML formatting
      let definitionRegex =
        /Definition at line <b>(\d+)<\/b> of file\s*<b>([^<]+)<\/b>[.!?]?/g;
      let _matches = Array.from(html.matchAll(definitionRegex));

      // Transform "Definition at line X of file Y" text to add GitHub links
      // Use flexible regex to match the actual pattern found (including optional trailing period)
      const transformRegex =
        /Definition at line[^<]*<b[^>]*>(\d+)<\/b>[^<]*of file[^<]*<b[^>]*>([^<]+)<\/b>[.!?]?/g;

      transformRegex.lastIndex = 0; // Reset lastIndex after test

      let result = html.replace(transformRegex, (match, lineNum, filename) => {
        let filepath = sourcePath || filename.trim();

        // Extract basename for lookup (works whether filepath is basename or full path)
        const basename = filepath.includes("/")
          ? filepath.substring(filepath.lastIndexOf("/") + 1)
          : filepath;

        // Look it up in manPages if available
        if (manPages && manPages.length > 0) {
          // Find all pages with this basename
          const candidatePages = manPages.filter((p) => p.name === basename);
          let page = null;

          if (candidatePages.length === 1) {
            // Only one file with this name - but check if content suggests a different file
            page = candidatePages[0];

            // Special case: if it's agent.c and content contains GPG markers, use GPG version
            const hasGPGMarkers =
              html.includes("GPG_AGENT") || html.includes("gpg_agent_");
            if (basename === "agent.c" && hasGPGMarkers) {
              // pages.json is incomplete - it doesn't have the GPG agent.c entry
              // So we hardcode the path when we detect GPG content
              filepath = "lib/crypto/gpg/agent.c";
              page = null; // Skip the lookup since we already set filepath
            }
          } else if (candidatePages.length > 1) {
            // Multiple files with same name - try to pick the right one
            // First, check if current page has the same basename
            if (selectedPageName) {
              const currentPage = manPages.find(
                (p) => p.name === selectedPageName,
              );
              if (currentPage && currentPage.name.endsWith(filepath)) {
                // Current page has matching basename, use it
                page = currentPage;
              } else if (currentPage && currentPage.sourcePath) {
                // Try to find a page from the same directory as current page
                const currentDir = currentPage.sourcePath.substring(
                  0,
                  currentPage.sourcePath.lastIndexOf("/"),
                );
                page = candidatePages.find((p) =>
                  p.sourcePath.startsWith(currentDir + "/"),
                );
              }
            }

            // If still not found, use the first candidate (fallback)
            if (!page) {
              page = candidatePages[0];
            }
          }

          if (page && page.sourcePath && !filepath.includes("/")) {
            // Only update filepath from page if it wasn't already set above
            filepath = page.sourcePath;
          } else if (!filepath.includes("/")) {
            // Map Doxygen file paths to GitHub paths if not found
            // video/rgba/image.h -> include/ascii-chat/video/rgba/image.h
            filepath = `include/ascii-chat/${filepath}`;
          } else if (
            filepath.includes("/") &&
            !filepath.startsWith("src/") &&
            !filepath.startsWith("include/") &&
            !filepath.startsWith("lib/")
          ) {
            // If it's a relative path like video/rgba/image.h, prepend include/ascii-chat/
            filepath = `include/ascii-chat/${filepath}`;
          }
        } else {
          // Fallback if manPages not available
          if (
            filepath &&
            !filepath.includes("/") &&
            (!manPages || manPages.length === 0)
          ) {
            // Fallback if manPages not available
            filepath = `include/ascii-chat/${filepath}`;
          } else if (
            filepath &&
            filepath.includes("/") &&
            !filepath.startsWith("src/") &&
            !filepath.startsWith("include/") &&
            !filepath.startsWith("lib/")
          ) {
            filepath = `include/ascii-chat/${filepath}`;
          }
        }

        const link =
          `<a href="https://github.com/zfogg/ascii-chat/blob/${githubRef}/${filepath}#L${lineNum}" ` +
          `target="_blank" rel="noopener noreferrer" class="text-cyan-400 hover:text-cyan-300 underline">` +
          `Definition at line <b>${lineNum}</b> of file <b>${filename}</b></a>`;
        return link;
      });

      // On source pages, transform line number anchors to GitHub links
      // e.g., <a id="l00022">22</a> becomes <a href="...#L22" ...>22</a>
      if (isSourcePage && sourcePath) {
        result = result.replace(
          /<a\s+id="l(\d+)"[^>]*>(\d+)<\/a>/g,
          (_, lineNum, displayNum) =>
            `<a id="l${lineNum}" href="https://github.com/zfogg/ascii-chat/blob/${githubRef}/${sourcePath}#L${lineNum}" ` +
            `target="_blank" rel="noopener noreferrer" class="text-cyan-400 hover:text-cyan-300">${displayNum}</a>`,
        );
      }

      return result;
    },
    [commitSha, manPages],
  );

  /**
   * Transform function P tags into tables
   * Handles multiple functions in a single P tag by splitting at function boundaries
   * Handles malformed HTML where signatures span multiple lines with <br/> tags
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

        // Check if this P tag contains a function signature (bold name followed by paren or br)
        if (!/<b>[a-zA-Z_][a-zA-Z0-9_]*<\/b>\s*[(<br]/.test(content)) {
          return match;
        }

        // Remove __attribute__ directives and visibility markers from content (various formats)
        content = content.replace(
          /__attribute__\s*\(\(\s*visibility\s*\(\s*['"]*\w+['"]*\s*\)\s*\)\)\s*/g,
          "",
        );
        content = content.replace(/__attribute__\s*\(\([^)]*\)\)\s*/g, "");
        content = content.replace(/__attribute__\s*\([^)]*\)\s*/g, "");

        // Check if this is the special case: function signature with <br/> delimiters
        // Pattern: bold function name followed by ( and incomplete params, then <br/>
        const brDelimiterPattern =
          /<b>([a-zA-Z_][a-zA-Z0-9_]*)<\/b>\s*\([^)]*<br\s*\/?>/;
        const hasBrDelimiters = brDelimiterPattern.test(content);

        const rows = [];

        if (hasBrDelimiters) {
          // Special handling for <br/> delimited functions
          // The uniqueness: function_name(<incomplete_params<br/>description<br/>
          // Find all such patterns to extract complete function signatures
          const funcPattern =
            /<b>([a-zA-Z_][a-zA-Z0-9_]*)<\/b>\s*\(([^<]*)<br\s*\/?>\s*([\s\S]*?)<br\s*\/?>/g;
          let funcMatch;

          while ((funcMatch = funcPattern.exec(content))) {
            const funcName = funcMatch[1];
            const incompleteParams = funcMatch[2].trim();
            const description = funcMatch[3].trim();
            const boldTagStartIdx = funcMatch.index; // Position of <b> in content

            // Extract the COMPLETE return type by looking at everything before <b>funcName</b>
            // This includes multi-word types like "const char *" or "__attribute__(...) size_t"
            const beforeBold = content.substring(0, boldTagStartIdx);

            // Remove all HTML tags from the before text to get pure content
            const tempDiv = document.createElement("div");
            tempDiv.innerHTML = beforeBold;
            const plainBefore = tempDiv.textContent;

            // Extract all non-whitespace tokens and take the last meaningful one(s) as return type
            const beforeTokens = plainBefore
              .trim()
              .split(/\s+/)
              .filter((t) => t);
            let returnType = "";

            if (beforeTokens.length > 0) {
              // Take last token(s) as return type, handling multi-word types
              returnType = beforeTokens[beforeTokens.length - 1];

              // Check if we need to combine with previous token (for cases like "char *")
              if (beforeTokens.length > 1) {
                const lastToken = beforeTokens[beforeTokens.length - 1];
                // If last token is * or const, combine with previous
                if (lastToken === "*" || lastToken === "const") {
                  returnType =
                    beforeTokens[beforeTokens.length - 2] + " " + lastToken;
                }
                // Also handle "const char *" pattern
                else if (
                  beforeTokens.length > 2 &&
                  beforeTokens[beforeTokens.length - 2] === "char" &&
                  beforeTokens[beforeTokens.length - 3] === "const"
                ) {
                  returnType =
                    beforeTokens[beforeTokens.length - 3] +
                    " " +
                    beforeTokens[beforeTokens.length - 2] +
                    " " +
                    lastToken;
                }
              }
            }

            // Extract complete parameters: everything from ( to <br/>
            const params = incompleteParams ? `(${incompleteParams}` : "(";

            // Build full signature with return type and function name
            const _fullSignature =
              (returnType ? returnType + " " : "") + funcName + params;

            // For table display, show just the params part without function name
            const signatureWithoutName = params;

            rows.push({
              name: funcName,
              signature: signatureWithoutName,
              description: description,
            });
          }
        } else {
          // Original logic for standard function parsing
          // Extract plain text for parsing, replacing <br> with space
          const tempDiv = document.createElement("div");
          tempDiv.innerHTML = content.replace(/<br\s*\/?>/gi, " ");
          const fullPlainText = tempDiv.textContent.trim();

          // Find ALL function names in the content (bold text followed by paren or br)
          const funcNameMatches = [];
          const boldRegex = /<b>([a-zA-Z_][a-zA-Z0-9_]*)<\/b>\s*[(<br]/g;
          let boldMatch;
          while ((boldMatch = boldRegex.exec(content))) {
            funcNameMatches.push(boldMatch[1]);
          }

          // If no functions found, return original
          if (funcNameMatches.length === 0) {
            return match;
          }

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

            // If closing paren not found, assume rest of content until next keyword
            if (closeParenIdx === -1) {
              // Look for common description starters or end of content
              const descPattern =
                /\s+(Calculate|Find|Get|Return|Check|Validate|Parse|Generate|Build|Create|Delete|Update|Process|Handle|Manage|Initialize|Configure|Set|Add|Remove|Extract|Transform|Convert|Encode|Decode|Encrypt|Decrypt|Serialize|Deserialize|Execute|Run|Start|Stop|Close|Open|Read|Write|Send|Receive|Send|Request|Response|Submit|Commit|Rollback|Allocate|Deallocate|Copy|Clone|Merge|Split|Join|Filter|Sort|Search|Lookup|Insert|Replace|Update|Append|Prepend|Reverse|Rotate|Shift|Swap|Compare|Match|Find|Locate|Register|Unregister|Subscribe|Unsubscribe|Attach|Detach|Link|Unlink|Mount|Unmount|Load|Unload|Import|Export|Include|Exclude|Enable|Disable|Activate|Deactivate|Suspend|Resume)/;
              const descMatch = functionContent
                .substring(openParenIdx)
                .search(descPattern);
              if (descMatch > 0) {
                closeParenIdx = openParenIdx + descMatch - 1;
              } else {
                // Fallback: use all content up to end
                closeParenIdx = functionContent.length - 1;
              }
            }

            // Extract signature and description
            let signature;
            let description;

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
                `\\b${currentMatch.name.replace(
                  /[.*+?^${}()|[\]\\]/g,
                  "\\$&",
                )}\\s*\\(`,
              ),
              "(",
            );

            rows.push({
              name: currentMatch.name,
              signature: signatureWithoutName,
              description: description,
            });
          }
        }

        // Build table with all rows
        if (rows.length === 0) {
          return match; // No functions parsed, return original
        }

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

      // Step 1.5: Add GitHub links BEFORE transforming functions (preserve "Definition at line" structure)
      processed = processDefinitionLinks(
        processed,
        sourcePath,
        commitSha,
        isSourcePage,
      );

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

      // Step 5: Transform filenames to man3 links (in HTML)
      processed = transformFilenameLinksInHTML(processed);

      // Step 6: Highlight search matches
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
    (html, searchQuery, sourcePath = "", isSourcePage = false) => {
      const bodyMatch = html.match(/<body[^>]*>([\s\S]*)<\/body>/i);
      let content = bodyMatch ? bodyMatch[1] : html;

      // Apply comprehensive preprocessing pipeline
      return preprocessPageHTML(
        content,
        searchQuery,
        sourcePath,
        commitSha,
        isSourcePage,
      );
    },
    [preprocessPageHTML, commitSha],
  );

  return {
    processPageContent,
    preprocessPageHTML,
    processDefinitionLinks,
  };
}
