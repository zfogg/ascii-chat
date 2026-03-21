import { useCallback } from "react";
import { highlightMatchesInHTML } from "../utils";
import type { MutableRefObject } from "react";

interface ManPage {
  name: string;
  sourcePath?: string;
}

interface MacroRow {
  name: string;
  value: string;
  description: string;
}

/**
 * Hook for all HTML string transformation functions
 * Used in the preprocessing pipeline to convert raw Doxygen HTML to clean documentation
 */
export function useHtmlTransforms(
  validPagesRef: MutableRefObject<Set<string>>,
  manPages: ManPage[],
  commitSha: string,
) {
  /**
   * Parse a macro match and convert to HTML table
   */
  const parseMacroMatch = useCallback((match: string): string | null => {
    const content = match.replace(/<p[^>]*>/, "").replace(/<\/p>/, "");
    const sections = content.split(/<br\s*\/?>/i);
    if (sections.length < 1) return null;

    const rows: MacroRow[] = [];
    let previousMacro: MacroRow | null = null;

    for (let i = 0; i < sections.length; i++) {
      const section = sections[i]!.trim();
      if (!section) continue;

      // Extract bold tags and text
      const boldRegex = /<b>([^<]+)<\/b>/g;
      const bolds: string[] = [];
      let boldMatch;
      while ((boldMatch = boldRegex.exec(section))) {
        bolds.push(boldMatch[1]!);
      }

      if (bolds.length === 0) continue;

      let name: string | undefined,
        value: string | undefined,
        description = "";

      if (bolds.length >= 1) {
        name = bolds[0]!;

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
    (html: string): string => {
      // Pattern 1: Standard sections that start with <br> (Password Requirements, etc.)
      let result = html.replace(
        /<p[^>]*class="Pp"[^>]*>\s*<br[^>]*>[\s\S]*?#define[\s\S]*?<\/p>/g,
        (match: string) => parseMacroMatch(match) || match,
      );

      // Pattern 2: Initial Macros section (starts directly with #define, allowing optional whitespace)
      // Only match if it contains #define statements
      result = result.replace(
        /<p[^>]*class="Pp"[^>]*>\s*#define[\s\S]*?<\/p>/g,
        (match: string) => {
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
    (html: string): string => {
      const fileRegex = /\b([a-zA-Z0-9_\-./]+\.(c|h|cpp|m|hpp))\b/g;

      return html.replace(fileRegex, (match: string, filename: string) => {
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
      html: string,
      sourcePath: string | undefined,
      passedCommitSha: string,
      isSourcePage = false,
      selectedPageName: string | null = null,
    ): string => {
      // Get the commit SHA - use passed value, then hook value, finally default to master
      const sha = passedCommitSha || commitSha;
      const githubRef = sha && sha !== "unknown" ? sha : "master";

      // Create fresh regex for each call to avoid state issues
      // Try multiple patterns to handle different HTML formatting
      const definitionRegex =
        /Definition at line <b>(\d+)<\/b> of file\s*<b>([^<]+)<\/b>[.!?]?/g;
      Array.from(html.matchAll(definitionRegex));

      // Transform "Definition at line X of file Y" text to add GitHub links
      // Use flexible regex to match the actual pattern found (including optional trailing period)
      const transformRegex =
        /Definition at line[^<]*<b[^>]*>(\d+)<\/b>[^<]*of file[^<]*<b[^>]*>([^<]+)<\/b>[.!?]?/g;

      transformRegex.lastIndex = 0; // Reset lastIndex after test

      let result = html.replace(
        transformRegex,
        (_match: string, lineNum: string, filename: string) => {
          let filepath = sourcePath || filename.trim();

          // Extract basename for lookup (works whether filepath is basename or full path)
          const basename = filepath.includes("/")
            ? filepath.substring(filepath.lastIndexOf("/") + 1)
            : filepath;

          // Look it up in manPages if available
          if (manPages && manPages.length > 0) {
            // Find all pages with this basename
            const candidatePages = manPages.filter(
              (p: ManPage) => p.name === basename,
            );
            let page: ManPage | null = null;

            if (candidatePages.length === 1) {
              // Only one file with this name - but check if content suggests a different file
              page = candidatePages[0] ?? null;

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
                  (p: ManPage) => p.name === selectedPageName,
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
                  page =
                    candidatePages.find((p: ManPage) =>
                      p.sourcePath?.startsWith(currentDir + "/"),
                    ) ?? null;
                }
              }

              // If still not found, use the first candidate (fallback)
              if (!page) {
                page = candidatePages[0] ?? null;
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
        },
      );

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
  const transformFunctionsInHTML = (html: string) => {
    // First, remove __attribute__ tags to clean up the HTML for regex matching
    let cleaned = html.replace(
      /<b>__attribute__<\/b>\s*\(\([^)]*\)\)+\s*/g,
      "",
    );

    // Find ALL P tags first, then check if they contain function signatures
    const result = cleaned.replace(
      /<p\s[^>]*>([\s\S]*?)<\/p>/g,
      (match: string, content: string) => {
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
            const incompleteParams = (funcMatch[2] ?? "").trim();
            const description = (funcMatch[3] ?? "").trim();
            const boldTagStartIdx = funcMatch.index; // Position of <b> in content

            // Extract the return type by looking for the first <b> tag before the function name
            // In Doxygen HTML, the pattern is: <b>returnType</b> <b>funcName</b> (...)
            const beforeBold = content.substring(0, boldTagStartIdx);
            const boldMatch = beforeBold.match(/<b>([^<]+)<\/b>\s*$/);
            let returnType = boldMatch ? (boldMatch[1] ?? "") : "";

            // Fallback: if no bold tag found, extract from plain text as before
            if (!returnType) {
              const tempDiv = document.createElement("div");
              tempDiv.innerHTML = beforeBold;
              const plainBefore = tempDiv.textContent ?? "";

              const beforeTokens = plainBefore
                .trim()
                .split(/\s+/)
                .filter((t) => t);

              if (beforeTokens.length > 0) {
                returnType = beforeTokens[beforeTokens.length - 1] ?? "";

                if (beforeTokens.length > 1) {
                  const lastToken = beforeTokens[beforeTokens.length - 1];
                  if (lastToken === "*" || lastToken === "const") {
                    returnType =
                      beforeTokens[beforeTokens.length - 2] + " " + lastToken;
                  } else if (
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
            }

            // Extract complete parameters: everything from ( to <br/>
            const params = incompleteParams ? `(${incompleteParams}` : "(";

            // Build full signature with return type and function name
            void ((returnType ? returnType + " " : "") + funcName + params);

            rows.push({
              name: funcName,
              signature: returnType || "void",
              description: description,
            });
          }
        } else {
          // Parse using HTML structure instead of plain text
          // Find all function names by extracting from bold tags
          const funcMatches = [];
          const boldRegex = /<b>([a-zA-Z_][a-zA-Z0-9_]*)<\/b>\s*\(/g;
          let boldMatch;
          while ((boldMatch = boldRegex.exec(content))) {
            funcMatches.push({
              name: boldMatch[1]!,
              htmlIndex: boldMatch.index,
            });
          }

          // If no functions found, return original
          if (funcMatches.length === 0) {
            return match;
          }

          // For each function, extract content from HTML between this function and the next
          for (let i = 0; i < funcMatches.length; i++) {
            const currentFunc = funcMatches[i]!;
            const nextFunc = funcMatches[i + 1];

            // Find the content span for this function: from current function to next function's <b> tag
            const contentStart = currentFunc.htmlIndex;
            const contentEnd = nextFunc ? nextFunc.htmlIndex : content.length;
            const htmlContent = content.substring(contentStart, contentEnd);

            // Extract full signature with return type and parameters
            // Pattern: "return_type <b>func_name</b>(...)"
            let returnType = "";
            let signature = "";
            const beforeFunc = content.substring(
              Math.max(0, currentFunc.htmlIndex - 150),
              currentFunc.htmlIndex,
            );

            // Try to match: "<b>type</b> <whitespace> end-of-string"
            const boldTypeMatch = beforeFunc.match(/<b>([^<]+)<\/b>\s*$/);
            if (boldTypeMatch && boldTypeMatch[1]) {
              const potentialType = boldTypeMatch[1];
              // Verify it's a type name (no * or multiple words, can contain underscores/numbers)
              if (
                !potentialType.includes("*") &&
                !potentialType.includes(" ")
              ) {
                returnType = potentialType;
              }
            }

            // If not found in bold, try plain text: "type <whitespace> end-of-string"
            if (!returnType) {
              const plainTypeMatch = beforeFunc.match(/(\w+)\s*$/);
              if (plainTypeMatch && plainTypeMatch[1]) {
                returnType = plainTypeMatch[1];
              }
            }

            // Extract the full signature: "return_type func_name(parameters)"
            // htmlContent starts with the function name in bold: <b>func_name</b> (params...)
            const paramsMatch = htmlContent.match(
              /<b>[^<]+<\/b>\s*(\([^)]*\))/,
            );
            if (paramsMatch && paramsMatch[1]) {
              // Extract parameters from the parentheses and clean HTML tags
              const params = paramsMatch[1]
                .replace(/<[^>]*>/g, "") // Remove HTML tags
                .replace(/\s+/g, " ")
                .trim();
              signature = `${returnType} ${currentFunc.name}${params}`;
            } else {
              // Fallback to just return type if we can't parse the signature
              signature = returnType || "void";
            }

            // Extract description: everything after the closing paren until the next <b> tag
            let description = "";
            const closeParenMatch = htmlContent.match(/\)([^)]*?)(?=<b>|$)/);
            if (closeParenMatch && closeParenMatch[1]) {
              description = closeParenMatch[1]
                .trim()
                // Remove HTML tags
                .replace(/<[^>]*>/g, "")
                // Clean up excess whitespace
                .replace(/\s+/g, " ")
                .trim();

              // Filter out descriptions that are just C type names (void, int, bool, etc.)
              // These are actually the return type of the next function, not a description
              const cTypeNames =
                /^(void|int|bool|long|short|char|float|double|unsigned|signed|const|struct|union|enum|typedef|uint\d+_t|int\d+_t|size_t|ssize_t|[a-z_]+_t)$/i;
              if (cTypeNames.test(description)) {
                description = "";
              }

              // Also remove trailing C type names that may have been captured from the next function's return type
              // e.g., "Some description. uint64_t" -> "Some description."
              description = description.replace(
                /\s+(void|int|bool|long|short|char|float|double|unsigned|signed|const|struct|union|enum|typedef|uint\d+_t|int\d+_t|size_t|ssize_t|[a-z_]+_t)\s*$/i,
                "",
              );
            }

            rows.push({
              name: currentFunc.name,
              signature: signature,
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
      html: string,
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
        (_match, _path, htmlFile, anchor) => {
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
    (
      html: string,
      searchQuery: string,
      sourcePath = "",
      isSourcePage = false,
    ) => {
      const bodyMatch = html.match(/<body[^>]*>([\s\S]*)<\/body>/i);
      let content = bodyMatch ? (bodyMatch[1] ?? html) : html;

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
