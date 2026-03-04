import { useMemo } from "react";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function Man({ html, isSourcePage = false }) {
  const decodeHtmlEntities = (text) => {
    const textarea = document.createElement("textarea");
    textarea.innerHTML = text;
    return textarea.value;
  };

  const extractCodeFromFragment = (fragmentHtml) => {
    const temp = document.createElement("div");
    temp.innerHTML = fragmentHtml;

    const lines = [];
    temp.querySelectorAll("div.line").forEach((lineDiv) => {
      const anchor = lineDiv.querySelector('a[id^="l"]');
      let lineNum = null;
      if (anchor) {
        const id = anchor.id;
        lineNum = parseInt(id.substring(1), 10);
      }

      const lineClone = lineDiv.cloneNode(true);
      lineClone
        .querySelectorAll('span.lineno, a[id^="l"], a[name^="l"]')
        .forEach((el) => el.remove());

      lineClone
        .querySelectorAll("div.foldopen, div.foldclose")
        .forEach((el) => {
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
  };

  const codeHasLineNumbers = (lines) => {
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

  const fragmentHasLineNumbers = (codeLines) => {
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
          if (preMatch[1].trim()) {
            elements.push(
              <div
                key={`html-${elements.length}`}
                className="man-page-html"
                dangerouslySetInnerHTML={{ __html: preMatch[1] }}
              />,
            );
          }

          let codeContent = preMatch[2];
          codeContent = decodeHtmlEntities(codeContent);
          if (codeContent.trim()) {
            const lines = codeContent.split("\n");
            const hasExistingLineNumbers = codeHasLineNumbers(lines);
            const maxLineNum = lines.length.toString().length;

            const hash = window.location.hash;
            let targetLineStart = null;
            let targetLineEnd = null;

            if (isSourcePage) {
              const rangeMatch = hash.match(/^#l(\d+)(?:-(\d+))?$/);
              if (rangeMatch) {
                targetLineStart = parseInt(rangeMatch[1], 10);
                targetLineEnd = rangeMatch[2]
                  ? parseInt(rangeMatch[2], 10)
                  : targetLineStart;
              }
            }

            if (targetLineStart !== null && isSourcePage) {
              const highlightedHtml = lines
                .map((text, idx) => {
                  const lineNum = idx + 1;
                  const paddedNum = String(lineNum).padStart(maxLineNum, " ");
                  const isTarget =
                    lineNum >= targetLineStart && lineNum <= targetLineEnd;

                  if (isTarget) {
                    return `<div style="background-color: #fbbf24; padding: 0.125rem 0.5rem;"><span style="font-family: monospace;">⟹ ${paddedNum}  ${text} ⟸</span></div>`;
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
                .map((text, idx) => {
                  const lineNum = idx + 1;
                  const paddedNum = String(lineNum).padStart(maxLineNum, " ");
                  if (hasExistingLineNumbers) {
                    return text;
                  }
                  return `    ${paddedNum}  ${text}`;
                })
                .join("\n");

              elements.push(
                <CodeBlock key={`code-${elements.length}`} language="c">
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
                targetLineStart = parseInt(rangeMatch[1], 10);
                targetLineEnd = rangeMatch[2]
                  ? parseInt(rangeMatch[2], 10)
                  : targetLineStart;

                if (
                  !codeLines.some((line) => {
                    const lineNum = line.number;
                    return (
                      lineNum >= targetLineStart && lineNum <= targetLineEnd
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
                const isTarget =
                  targetLineStart !== null &&
                  line.number >= targetLineStart &&
                  line.number <= targetLineEnd;

                if (hasExistingLineNumbers) {
                  if (isTarget) {
                    return `⟹ ${line.text} ⟸`;
                  }
                  return line.text;
                }

                if (isTarget) {
                  return `⟹ ${paddedNum}  ${line.text} ⟸`;
                }
                return `    ${paddedNum}  ${line.text}`;
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
                <CodeBlock language="c">{codeWithLineNumbers}</CodeBlock>
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
                    if (span.textContent.includes("⟹")) {
                      span.style.backgroundColor = "#fbbf24";
                      span.style.color = "#000";

                      for (let i = spanIdx + 1; i < allSpans.length; i++) {
                        const nextSpan = allSpans[i];
                        if (nextSpan.textContent.includes("\n")) {
                          if (nextSpan.textContent.includes("⟸")) {
                            nextSpan.style.backgroundColor = "#fbbf24";
                            nextSpan.style.color = "#000";
                          }
                          break;
                        }
                        nextSpan.style.backgroundColor = "#fbbf24";
                        nextSpan.style.color = "#000";
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
        : [<div dangerouslySetInnerHTML={{ __html: html }} />];
    };
  }, [html, isSourcePage]);

  return <>{renderContentWithCodeBlocks()}</>;
}
