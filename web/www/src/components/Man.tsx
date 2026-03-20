import { useMemo } from "react";
import { CodeBlock } from "@ascii-chat/shared/components";

interface ManProps {
  html: string;
  isSourcePage?: boolean;
  showLineNumbers?: boolean;
}

interface CodeLine {
  text: string;
  number: number | null;
}

export default function Man({
  html,
  isSourcePage = false,
  showLineNumbers = false,
}: ManProps) {
  const decodeHtmlEntities = (text: string): string => {
    const textarea = document.createElement("textarea");
    textarea.innerHTML = text;
    return textarea.value;
  };

  const extractCodeFromFragment = (fragmentHtml: string): CodeLine[] => {
    const temp = document.createElement("div");
    temp.innerHTML = fragmentHtml;

    const lines: CodeLine[] = [];
    temp.querySelectorAll("div.line").forEach((lineDiv) => {
      const anchor = lineDiv.querySelector('a[id^="l"]');
      let lineNum: number | null = null;
      if (anchor) {
        const id = anchor.id;
        lineNum = parseInt(id.substring(1), 10);
      }

      const lineClone = lineDiv.cloneNode(true) as HTMLElement;
      (lineClone as HTMLElement)
        .querySelectorAll('span.lineno, a[id^="l"], a[name^="l"]')
        .forEach((el) => el.remove());

      (lineClone as HTMLElement)
        .querySelectorAll("div.foldopen, div.foldclose")
        .forEach((el) => {
          const parent = el.parentNode;
          if (parent) {
            while (el.firstChild) {
              parent.insertBefore(el.firstChild, el);
            }
            parent.removeChild(el);
          }
        });

      const lineText = (lineClone.textContent || "").trimEnd();
      if (lineText) {
        lines.push({ text: lineText, number: lineNum });
      }
    });

    return lines;
  };

  const codeHasLineNumbers = (lines: string[]): boolean => {
    if (lines.length === 0) return false;
    const lineNumberPattern = /^\s*\d{1,5}\s{1,3}\S/;
    let checkedLines = 0;
    for (const line of lines) {
      if (line.trim().length > 0) {
        if (!lineNumberPattern.test(line)) {
          return false;
        }
        checkedLines++;
        if (checkedLines >= 3) break;
      }
    }
    return checkedLines > 0;
  };

  const fragmentHasLineNumbers = (codeLines: CodeLine[]): boolean => {
    if (codeLines.length === 0) return false;
    const linesWithNumbers = codeLines.filter(
      (line) => line.number !== null,
    ).length;
    return linesWithNumbers > codeLines.length * 0.8;
  };

  const renderContentWithCodeBlocks = useMemo(() => {
    return () => {
      const elements = [];
      let remaining = html;

      while (remaining.length > 0) {
        const preMatch = remaining.match(/^([\s\S]*?)<pre>([\s\S]*?)<\/pre>/);
        if (preMatch) {
          if (preMatch[1] && preMatch[1].trim()) {
            elements.push(
              <div
                key={`html-${elements.length}`}
                className="man-page-html"
                dangerouslySetInnerHTML={{ __html: preMatch[1] }}
              />,
            );
          }

          let codeContent = preMatch[2]!;
          codeContent = decodeHtmlEntities(codeContent);
          // Remove leading/trailing whitespace from entire code block
          codeContent = codeContent.trim();
          if (codeContent) {
            let lines = codeContent.split("\n");
            // Remove any empty lines from start and end
            while (lines.length > 0 && lines[0]!.trim() === "") {
              lines.shift();
            }
            while (lines.length > 0 && lines[lines.length - 1]!.trim() === "") {
              lines.pop();
            }
            const hasExistingLineNumbers = codeHasLineNumbers(lines);
            const maxLineNum = lines.length.toString().length;

            const hash = window.location.hash;
            let targetLineStart = null;
            let targetLineEnd = null;

            if (isSourcePage) {
              const rangeMatch = hash.match(/^#l(\d+)(?:-(\d+))?$/);
              if (rangeMatch) {
                targetLineStart = parseInt(rangeMatch[1]!, 10);
                targetLineEnd = rangeMatch[2]
                  ? parseInt(rangeMatch[2], 10)
                  : targetLineStart;
              }
            }

            if (targetLineStart !== null && isSourcePage) {
              const highlightedHtml = lines
                .map((text: string, idx: number) => {
                  const lineNum = idx + 1;
                  const paddedNum = String(lineNum).padStart(maxLineNum, " ");
                  const isTarget =
                    lineNum >= targetLineStart! && lineNum <= targetLineEnd!;

                  if (isTarget) {
                    return `<div style="background-color: var(--highlight-color); padding: 0.125rem 0.5rem;"><span style="font-family: monospace;">${paddedNum}  ${text}</span></div>`;
                  }
                  return `<div style="font-family: monospace;"><span>    ${paddedNum}  ${text}</span></div>`;
                })
                .join("");

              elements.push(
                <div
                  key={`code-${elements.length}`}
                  style={{
                    fontSize: "0.875rem",
                    lineHeight: "1.5",
                    backgroundColor: "#111827",
                    padding: "1rem",
                    borderRadius: "0.5rem",
                    overflow: "auto",
                    marginBottom: "1rem",
                  }}
                  dangerouslySetInnerHTML={{ __html: highlightedHtml }}
                />,
              );
            } else {
              const codeWithLineNumbers = lines
                .map((text: string, idx: number) => {
                  const lineNum = idx + 1;
                  const paddedNum = String(lineNum).padStart(maxLineNum, " ");
                  if (hasExistingLineNumbers) {
                    return text;
                  }
                  if (!showLineNumbers) {
                    return text;
                  }
                  // Keep content aligned at same column regardless of line number width
                  const leadingSpace = " ".repeat(
                    Math.max(0, 4 - (maxLineNum - 1)),
                  );
                  return `${leadingSpace}${paddedNum}  ${text}`;
                })
                .join("\n");

              elements.push(
                <CodeBlock
                  key={`code-${elements.length}`}
                  language="c"
                  showLineNumbers={showLineNumbers}
                >
                  {codeWithLineNumbers}
                </CodeBlock>,
              );
            }
          }

          remaining = remaining.substring(preMatch[0].length);
          continue;
        }

        const fragmentStart = remaining.indexOf('<div class="fragment">');
        if (fragmentStart !== -1) {
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

          let depth = 0;
          let fragmentEnd = fragmentStart;

          for (let i = fragmentStart; i < remaining.length; i++) {
            if (remaining[i] === "<") {
              if (remaining[i + 1] === "/") {
                if (remaining.substring(i, i + 6) === "</div>") {
                  depth--;
                  if (depth === 0) {
                    fragmentEnd = i + 6;
                    break;
                  }
                }
              } else if (remaining.substring(i, i + 5) === "<div ") {
                depth++;
              }
            }
          }

          const fragmentHtml = remaining.substring(fragmentStart, fragmentEnd);

          const codeLines = extractCodeFromFragment(fragmentHtml);
          if (codeLines.length > 0) {
            const maxLineNum =
              codeLines.length > 0
                ? Math.max(
                    ...codeLines.map(
                      (line) => (line.number || 0).toString().length,
                    ),
                  )
                : 1;

            const hasExistingLineNumbers = fragmentHasLineNumbers(codeLines);

            const hash = window.location.hash;
            let targetLineStart = null;
            let targetLineEnd = null;

            if (isSourcePage) {
              const rangeMatch = hash.match(/^#l(\d+)(?:-(\d+))?$/);
              if (rangeMatch) {
                targetLineStart = parseInt(rangeMatch[1]!, 10);
                targetLineEnd = rangeMatch[2]
                  ? parseInt(rangeMatch[2], 10)
                  : targetLineStart;

                if (
                  !codeLines.some((line) => {
                    const lineNum = line.number;
                    return (
                      lineNum !== null &&
                      lineNum >= targetLineStart! &&
                      lineNum <= targetLineEnd!
                    );
                  })
                ) {
                  targetLineStart = null;
                  targetLineEnd = null;
                }
              }
            }

            const codeWithLineNumbers = codeLines
              .map((line) => {
                const lineNum = line.number || "";
                const paddedNum = String(lineNum).padStart(maxLineNum, " ");

                if (hasExistingLineNumbers) {
                  return line.text;
                }

                if (!showLineNumbers) {
                  return line.text;
                }

                // Keep content aligned at same column regardless of line number width
                const leadingSpace = " ".repeat(
                  Math.max(0, 4 - (maxLineNum - 1)),
                );
                return `${leadingSpace}${paddedNum}  ${line.text}`;
              })
              .join("\n");

            elements.push(
              <div
                key={`code-wrapper-${elements.length}`}
                className="code-with-highlight"
              >
                <style>{`
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
                `}</style>
                <CodeBlock language="c" showLineNumbers={showLineNumbers}>
                  {codeWithLineNumbers}
                </CodeBlock>
              </div>,
            );

            if (targetLineStart !== null && isSourcePage) {
              setTimeout(() => {
                const codeBlocks = document.querySelectorAll(
                  ".code-with-highlight pre code",
                );
                codeBlocks.forEach((block) => {
                  const allSpans = Array.from(block.querySelectorAll("span"));

                  allSpans.forEach((span, spanIdx) => {
                    if (span.textContent?.includes("⟹")) {
                      span.style.backgroundColor = "var(--highlight-color)";
                      span.style.color = "var(--text-highlight)";

                      for (let i = spanIdx + 1; i < allSpans.length; i++) {
                        const nextSpan = allSpans[i];
                        if (nextSpan && nextSpan.textContent?.includes("\n")) {
                          if (nextSpan.textContent?.includes("⟸")) {
                            nextSpan.style.backgroundColor =
                              "var(--highlight-color)";
                            nextSpan.style.color = "var(--text-highlight)";
                          }
                          break;
                        }
                        if (nextSpan) {
                          nextSpan.style.backgroundColor =
                            "var(--highlight-color)";
                          nextSpan.style.color = "var(--text-highlight)";
                        }
                      }
                    }
                  });
                });
              }, 0);
            }
          }

          elements.push(
            <div
              key={`anchors-${elements.length}`}
              style={{ display: "none" }}
              dangerouslySetInnerHTML={{ __html: fragmentHtml }}
            />,
          );

          remaining = remaining.substring(fragmentEnd);
          continue;
        }

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

      return elements.length > 0
        ? elements
        : [
            <div
              key="fallback-html"
              dangerouslySetInnerHTML={{ __html: html }}
            />,
          ];
    };
  }, [html, isSourcePage, showLineNumbers]);

  return <>{renderContentWithCodeBlocks()}</>;
}
