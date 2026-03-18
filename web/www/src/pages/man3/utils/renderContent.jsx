import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

/**
 * Decode HTML entities using browser's HTML parser
 */
function decodeHtmlEntities(text) {
  const textarea = document.createElement("textarea");
  textarea.innerHTML = text;
  return textarea.value;
}

/**
 * Extract code lines from a Doxygen fragment div
 * Returns array of { text, number } objects where number is the source line number
 */
function extractCodeFromFragment(fragmentHtml) {
  // Create a temporary DOM element to parse the HTML
  const temp = document.createElement("div");
  temp.innerHTML = fragmentHtml;

  // Extract line numbers and code together
  const lines = [];
  temp.querySelectorAll("div.line").forEach((lineDiv) => {
    // Get the anchor ID if present (e.g., "l00044")
    const anchor = lineDiv.querySelector('a[id^="l"]');
    let lineNum = null;
    if (anchor) {
      const id = anchor.id;
      // Convert "l00044" to "44"
      lineNum = parseInt(id.substring(1), 10);
    }

    // Clone the line and remove anchors/line numbers for text extraction
    const lineClone = lineDiv.cloneNode(true);
    lineClone
      .querySelectorAll('span.lineno, a[id^="l"], a[name^="l"]')
      .forEach((el) => el.remove());

    // Unwrap fold divs in the clone
    lineClone.querySelectorAll("div.foldopen, div.foldclose").forEach((el) => {
      const parent = el.parentNode;
      while (el.firstChild) {
        parent.insertBefore(el.firstChild, el);
      }
      parent.removeChild(el);
    });

    const lineText = lineClone.textContent.trimEnd();
    if (lineText) {
      lines.push({ text: lineText, number: lineNum });
    }
  });

  return lines;
}

/**
 * Convert HTML containing pre and fragment blocks into JSX with CodeBlock components
 *
 * @param {string} html - The HTML to render
 * @param {boolean} isSourcePage - Whether this is a source code page
 * @param {string} searchQuery - The current search query (for highlighting)
 * @param {number} targetLineNum - Line number to highlight (optional)
 * @returns {JSX.Element[]} - Array of React elements
 */
export function renderContentWithCodeBlocks(
  html,
  isSourcePage = false,
  searchQuery = "",
  targetLineNum = null,
) {
  const elements = [];
  let remaining = html;

  while (remaining.length > 0) {
    // Try to find a pre tag
    const preMatch = remaining.match(/^([\s\S]*?)<pre>([\s\S]*?)<\/pre>/);
    if (preMatch) {
      // Add HTML before pre tag
      if (preMatch[1].trim()) {
        elements.push(
          <div
            key={`html-${elements.length}`}
            className="man-page-html"
            dangerouslySetInnerHTML={{ __html: preMatch[1] }}
          />,
        );
      }

      // Add code block with line numbers
      let codeContent = preMatch[2];

      // Strip search highlighting to allow CodeBlock to tokenize properly
      const cleanedContent = codeContent
        .replace(
          /<span[^>]*style="[^"]*background-color:\s*#fbbf24[^"]*"[^>]*>/g,
          "",
        )
        .replace(/<span[^>]*class="[^"]*bg-yellow-900[^"]*"[^>]*>/g, "")
        .replace(/<\/span>/g, "");

      // Decode to plain text
      const decodedContent = decodeHtmlEntities(cleanedContent);
      if (decodedContent.trim()) {
        // Check if the pre tag contains source line numbers
        // and extract requested line based on hash anchor or targetLineNum parameter
        let targetLineStart = null;
        let targetLineEnd = null;

        // Use targetLineNum parameter if provided, otherwise check hash
        let requestedLineNum = targetLineNum;
        if (!requestedLineNum && isSourcePage) {
          const hash = window.location.hash;
          if (hash.match(/^#l(\d+)/)) {
            const rangeMatch = hash.match(/^#l(\d+)(?:-(\d+))?$/);
            if (rangeMatch) {
              requestedLineNum = parseInt(rangeMatch[1], 10);
            }
          }
        }

        if (isSourcePage && requestedLineNum !== null) {
          const lines = decodedContent.split("\n");
          // Check if lines start with line number format: "00001 code" or similar
          const lineNumPattern = /^(\d+)\s+/;
          const firstLineMatch = lines[0]?.match(lineNumPattern);

          if (firstLineMatch) {
            // Try to find the requested line
            for (let i = 0; i < lines.length; i++) {
              const match = lines[i].match(lineNumPattern);
              if (match) {
                const lineNum = parseInt(match[1], 10);
                if (lineNum === requestedLineNum) {
                  targetLineStart = i + 1; // 1-indexed position in this code block
                  targetLineEnd = i + 1; // Highlight just the one line
                  break;
                }
              }
            }
          }
        }

        const lines = decodedContent.split("\n");

        // Extract source line numbers from pre-tag content (format: "00123 code")
        let firstSourceLineNum = null;
        let lastSourceLineNum = null;
        const lineNumPattern = /^(\d+)\s+/;
        for (const line of lines) {
          const match = line.match(lineNumPattern);
          if (match) {
            const lineNum = parseInt(match[1], 10);
            if (firstSourceLineNum === null) {
              firstSourceLineNum = lineNum;
            }
            lastSourceLineNum = lineNum;
          }
        }

        const codeWithLineNumbers = lines.join("\n");

        // Use CodeBlock with line highlighting
        // Add an id to the container if this block has a target line for scrolling
        const blockId = targetLineStart
          ? `code-block-${targetLineStart}`
          : undefined;

        elements.push(
          <div
            key={`code-${elements.length}`}
            id={blockId}
            className="code-with-highlight"
            data-first-line={firstSourceLineNum}
            data-last-line={lastSourceLineNum}
          >
            <CodeBlock
              highlightLines={
                targetLineStart
                  ? { start: targetLineStart, end: targetLineEnd }
                  : undefined
              }
              searchQuery={searchQuery}
              showLineNumbers={true}
            >
              {codeWithLineNumbers}
            </CodeBlock>
          </div>,
        );
      }

      // Update remaining to after the pre tag
      remaining = remaining.substring(preMatch[0].length);
      continue;
    }

    // Try to find a fragment div
    const fragmentStart = remaining.indexOf('<div class="fragment">');
    if (fragmentStart !== -1) {
      // Add HTML before fragment
      if (fragmentStart > 0) {
        const htmlBefore = remaining.substring(0, fragmentStart);
        if (htmlBefore.trim()) {
          elements.push(
            <div
              key={`html-${elements.length}`}
              className="man-page-html"
              dangerouslySetInnerHTML={{ __html: htmlBefore }}
            />,
          );
        }
      }

      // Find matching closing div by counting open/close tags
      let depth = 0;
      let fragmentEnd = fragmentStart;

      for (let i = fragmentStart; i < remaining.length; i++) {
        if (remaining[i] === "<") {
          // Check if it's opening or closing
          if (remaining[i + 1] === "/") {
            // Closing tag
            if (remaining.substring(i, i + 6) === "</div>") {
              depth--;
              if (depth === 0) {
                fragmentEnd = i + 6;
                break;
              }
            }
          } else if (remaining.substring(i, i + 5) === "<div ") {
            // Opening div tag
            depth++;
          }
        }
      }

      // Extract fragment and preserve anchors separately for scrolling
      const fragmentHtml = remaining.substring(fragmentStart, fragmentEnd);

      // Render the code block with line numbers
      const codeLines = extractCodeFromFragment(fragmentHtml);
      if (codeLines.length > 0) {
        // Extract target line number(s) from hash if present
        const hash = window.location.hash;
        let targetLineStart = null;
        let targetLineEnd = null;

        if (isSourcePage) {
          // Match both single line (#l00423) and range (#l00423-00458)
          const rangeMatch = hash.match(/^#l(\d+)(?:-(\d+))?$/);
          if (rangeMatch) {
            targetLineStart = parseInt(rangeMatch[1], 10);
            targetLineEnd = rangeMatch[2]
              ? parseInt(rangeMatch[2], 10)
              : targetLineStart;

            // Only set if at least one line exists in the code
            if (
              !codeLines.some((line) => {
                const lineNum = line.number;
                return lineNum >= targetLineStart && lineNum <= targetLineEnd;
              })
            ) {
              targetLineStart = null;
              targetLineEnd = null;
            }
          }
        }

        // Build code (line numbers handled by CodeBlock)
        const codeWithLineNumbers = codeLines
          .map((line) => line.text)
          .join("\n");

        // Add an id to the wrapper if this block has a target line for scrolling
        const wrapperId = targetLineStart
          ? `code-block-${targetLineStart}`
          : undefined;
        // Also add data attribute to track which source lines are in this block
        const firstLineNum = codeLines.length > 0 ? codeLines[0].number : null;
        const lastLineNum =
          codeLines.length > 0 ? codeLines[codeLines.length - 1].number : null;

        elements.push(
          <div
            key={`code-wrapper-${elements.length}`}
            id={wrapperId}
            className="code-with-highlight"
            data-first-line={firstLineNum}
            data-last-line={lastLineNum}
          >
            <style>
              {`
              .code-with-highlight pre code {
                display: block;
              }
              .code-with-highlight pre {
                position: relative;
              }
              .code-with-highlight pre::before {
                content: '';
                position: absolute;
                left: 0;
                right: 0;
                pointer-events: none;
              }
            `}
            </style>
            <CodeBlock searchQuery={searchQuery} showLineNumbers={true}>
              {codeWithLineNumbers}
            </CodeBlock>
          </div>,
        );
      }

      // Also render the original fragment HTML hidden so anchors are in DOM for scrolling
      elements.push(
        <div
          key={`anchors-${elements.length}`}
          style={{ display: "none" }}
          dangerouslySetInnerHTML={{ __html: fragmentHtml }}
        />,
      );

      // Update remaining
      remaining = remaining.substring(fragmentEnd);
      continue;
    }

    // No more code blocks, add remaining HTML
    if (remaining.trim()) {
      elements.push(
        <div
          key={`html-${elements.length}`}
          className="man-page-html"
          dangerouslySetInnerHTML={{ __html: remaining }}
        />,
      );
    }
    break;
  }

  return elements.length > 0 ? (
    elements
  ) : (
    <div dangerouslySetInnerHTML={{ __html: html }} />
  );
}
