import { useState, useEffect, useRef } from "react";
import Footer from "../components/Footer";
import { setBreadcrumbSchema } from "../utils/breadcrumbs";
import { AsciiChatHead } from "../components/AsciiChatHead";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function Man3() {
  const [manPages, setManPages] = useState([]);
  const [searchQuery, setSearchQuery] = useState("");
  const [searchResults, setSearchResults] = useState([]);
  const [filesMatched, setFilesMatched] = useState(0);
  const [totalMatches, setTotalMatches] = useState(0);
  const [loading, setLoading] = useState(true);
  const [searching, setSearching] = useState(false);
  const [selectedPageContent, setSelectedPageContent] = useState(null);
  const [selectedPageName, setSelectedPageName] = useState(null);
  const [targetLineNumber, setTargetLineNumber] = useState(null);
  const [targetSnippetIndex, setTargetSnippetIndex] = useState(null);
  const searchTimeoutRef = useRef(null);
  const contentViewerRef = useRef(null);

  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "API Reference", path: "/man3" },
    ]);

    // Load from URL params
    const params = new URLSearchParams(window.location.search);
    const queryParam = params.get("q");
    const pageParam = params.get("page");

    if (queryParam) {
      setSearchQuery(decodeURIComponent(queryParam));
    }

    if (pageParam) {
      const pageName = decodeURIComponent(pageParam);
      setSelectedPageName(pageName);

      // Preserve hash for scrolling after content loads
      const hash = window.location.hash;

      fetch(`/man3/${pageName}.html`)
        .then((r) => r.text())
        .then((html) => {
          // Extract stylesheets from head
          const stylesheets = [];
          const styleMatches = html.matchAll(
            /<link[^>]*rel="stylesheet"[^>]*href="([^"]+)"[^>]*>/gi,
          );
          for (const match of styleMatches) {
            const href = match[1];
            if (!href.startsWith("/") && !href.startsWith("http")) {
              stylesheets.push(`<link rel="stylesheet" href="/man3/${href}" />`);
            } else {
              stylesheets.push(match[0]);
            }
          }

          // Process content (URLs and highlighting)
          const processedContent = processPageContent(html, "");

          // Prepend stylesheets
          const content = stylesheets.join("\n") + processedContent;
          setSelectedPageContent(content);
        })
        .catch((e) => console.error("Failed to load page:", e));
    }
  }, []);

  // Load man3 index
  useEffect(() => {
    fetch("/man3/index.json")
      .then((r) => r.json())
      .then((pages) => {
        setManPages(pages);
        setLoading(false);
        // Show all pages initially
        setSearchResults(pages);
      })
      .catch((e) => {
        console.error("Failed to load man3 index:", e);
        setLoading(false);
      });
  }, []);

  // Debounced search via API
  useEffect(() => {
    if (searchTimeoutRef.current) {
      clearTimeout(searchTimeoutRef.current);
    }

    searchTimeoutRef.current = setTimeout(async () => {
      if (!searchQuery.trim()) {
        setSearchResults(manPages);
        setFilesMatched(0);
        setTotalMatches(0);
        setSearching(false);
        // Clear search param but preserve page param if present
        const params = new URLSearchParams(window.location.search);
        params.delete("q");
        const newUrl = params.toString()
          ? `/man3?${params.toString()}`
          : "/man3";
        window.history.replaceState({}, "", newUrl + window.location.hash);
        return;
      }

      setSearching(true);

      try {
        const response = await fetch(
          `/api/man3/search?q=${encodeURIComponent(searchQuery)}`,
        );
        const data = await response.json();

        if (data.error) {
          setSearchResults([]);
          setFilesMatched(0);
          setTotalMatches(0);
        } else {
          setSearchResults(data.results || []);
          setFilesMatched(data.filesMatched || 0);
          setTotalMatches(data.totalMatches || 0);
        }

        // Update URL with search query (preserve page param if present)
        const params = new URLSearchParams(window.location.search);
        params.set("q", searchQuery);
        window.history.replaceState({}, "", `/man3?${params.toString()}` + window.location.hash);
      } catch (e) {
        console.error("Search error:", e);
        setSearchResults([]);
        setFilesMatched(0);
        setTotalMatches(0);
      } finally {
        setSearching(false);
      }
    }, 500);

    return () => {
      if (searchTimeoutRef.current) {
        clearTimeout(searchTimeoutRef.current);
      }
    };
  }, [searchQuery, manPages]);

  // Helper function to process HTML content: convert URLs and highlight matches
  const processPageContent = (html, searchQuery) => {
    const bodyMatch = html.match(/<body[^>]*>([\s\S]*)<\/body>/i);
    let content = bodyMatch ? bodyMatch[1] : html;

    // Fix relative paths to absolute paths for images and links
    content = content.replace(/src="([^"]+)"/g, (match, src) => {
      if (!src.startsWith("/") && !src.startsWith("http")) {
        return `src="/man3/${src}"`;
      }
      return match;
    });
    content = content.replace(/href="([^"]+)"/g, (match, href) => {
      if (
        !href.startsWith("/") &&
        !href.startsWith("http") &&
        !href.startsWith("#")
      ) {
        return `href="/man3/${href}"`;
      }
      return match;
    });

    // Convert all HTML file links to Man3 links
    content = content.replace(
      /href="(?:https?:\/\/[^\/]+)?([^"]*\/)?([^\/".]+\.html)(#l\d+)?"/gi,
      (match, path, htmlFile, anchor) => {
        const newPageName = htmlFile.replace(".html", "");
        const newHref = `/man3?page=${newPageName}${anchor || ""}`;
        return `href="${newHref}"`;
      },
    );

    // Preserve empty anchor tags by adding zero-width space
    content = content.replace(/<a\s+id="(l\d+)"[^>]*>\s*<\/a>/g, '<a id="$1">\u200B</a>');

    // Highlight matches in the processed content
    return highlightMatchesInHTML(content, searchQuery);
  };

  const loadPageContent = (
    pageName,
    lineNumber = null,
    snippetIndex = null,
  ) => {
    if (selectedPageName === pageName && lineNumber === null) {
      // Toggle off if clicking same page
      setSelectedPageName(null);
      setSelectedPageContent(null);
      setTargetLineNumber(null);
      setTargetSnippetIndex(null);
      // Remove page param from URL
      const params = new URLSearchParams(window.location.search);
      params.delete("page");
      const newUrl = params.toString() ? `/man3?${params.toString()}` : "/man3";
      window.history.replaceState({}, "", newUrl);
      return;
    }

    fetch(`/man3/${pageName}.html`)
      .then((r) => r.text())
      .then((html) => {
        const processedContent = processPageContent(html, searchQuery);
        setSelectedPageContent(processedContent);
        setSelectedPageName(pageName);
        if (lineNumber) {
          setTargetLineNumber(lineNumber);
          setTargetSnippetIndex(snippetIndex);
        }
        // Update URL with selected page param
        const params = new URLSearchParams(window.location.search);
        params.set("page", pageName);
        // Only remove line number hashes (#l0000 or #l0000-00000) when not targeting a line
        // Preserve any other hashes
        let hash = window.location.hash;
        if (!lineNumber && /#l\d+(?:-\d+)?$/.test(hash)) {
          hash = ""; // Remove line number hash
        }
        window.history.replaceState({}, "", `/man3?${params.toString()}` + hash);
      })
      .catch((e) => console.error("Failed to load page:", e));
  };

  const highlightMatches = (text, query) => {
    if (!query.trim()) return text;

    try {
      const regex = new RegExp(`(${query})`, "gi");
      const parts = text.split(regex);

      return parts.map((part, i) => {
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
    } catch (e) {
      return text;
    }
  };

  const highlightMatchesInHTML = (html, query) => {
    if (!query.trim()) return html;

    try {
      const regex = new RegExp(`(${query})`, "gi");
      // Split by HTML tags, only highlight text content (not tag content)
      const parts = html.split(/(<[^>]*>)/);

      const highlighted = parts
        .map((part) => {
          if (part.startsWith("<")) {
            // It's a tag, don't modify
            return part;
          } else {
            // It's text content, highlight matches
            return part.replace(
              regex,
              '<span class="bg-yellow-900/50 text-yellow-200">$1</span>',
            );
          }
        })
        .join("");

      return highlighted;
    } catch (e) {
      return html;
    }
  };

  // Scroll to target line when content loads
  useEffect(() => {
    if (selectedPageContent && targetLineNumber && contentViewerRef.current) {
      // Delay scroll to allow content to render
      setTimeout(() => {
        const viewer = contentViewerRef.current;
        if (viewer) {
          // Find all highlighted matches in the page
          const highlights = viewer.querySelectorAll(
            ".man-page-content span.bg-yellow-900\\/50",
          );

          // Use snippet index if available, otherwise find first match near target line
          let targetHighlight = null;
          if (
            targetSnippetIndex !== null &&
            highlights.length > targetSnippetIndex
          ) {
            targetHighlight = highlights[targetSnippetIndex];
          } else if (highlights.length > 0) {
            targetHighlight = highlights[0];
          }

          if (targetHighlight) {
            // Get element's position relative to the scrollable container
            const elementTop = targetHighlight.offsetTop;

            // Center it in the viewport
            const viewportCenter = viewer.clientHeight / 2;
            const scrollTop = Math.max(0, elementTop - viewportCenter);

            viewer.scrollTop = scrollTop;
          }
        }
      }, 300);
      setTargetLineNumber(null);
      setTargetSnippetIndex(null);
    }
  }, [targetLineNumber]);

  // Handle Doxygen link interception and line number scrolling
  useEffect(() => {
    if (!contentViewerRef.current) return;

    const handleLinkClick = (e) => {
      const link = e.target.closest("a");
      if (!link) return;

      const href = link.getAttribute("href");
      if (!href) return;

      // Match Doxygen source file links: /man3/filename.html#l00123
      const doxygenMatch = href.match(
        /\/man3\/(.+?)\.html(#l\d+)?$/,
      );
      if (doxygenMatch) {
        e.preventDefault();
        const pageName = doxygenMatch[1];
        const lineAnchor = doxygenMatch[2] || "";

        // Load the page and scroll to line if specified
        loadPageContent(pageName);

        // If there's a line anchor, scroll to it after content loads
        if (lineAnchor) {
          setTimeout(() => {
            if (contentViewerRef.current) {
              const element = contentViewerRef.current.querySelector(
                lineAnchor,
              );
              if (element) {
                element.scrollIntoView({ behavior: "smooth" });
              }
            }
          }, 300);
        }
      }
    };

    contentViewerRef.current.addEventListener("click", handleLinkClick);
    return () => {
      contentViewerRef.current?.removeEventListener("click", handleLinkClick);
    };
  }, []);

  // Scroll to hash fragment when page content changes
  useEffect(() => {
    if (!selectedPageContent || !contentViewerRef.current) return;

    const hash = window.location.hash;
    if (!hash) return;

    // Try to find and scroll to the first line with the arrow marker
    const scrollToHash = () => {
      const container = contentViewerRef.current;

      // Look for the first arrow marker (⟹) in visible code blocks
      const codeBlocks = container.querySelectorAll("pre code");
      for (const block of codeBlocks) {
        const spans = block.querySelectorAll("span");
        for (const span of spans) {
          if (span.textContent.includes("⟹")) {
            // Found the first arrow, scroll it to the center of the viewport
            span.scrollIntoView({ block: "center" });
            return true;
          }
        }
      }
      return false;
    };

    // Try multiple times in case elements are still rendering
    let attempts = 0;
    const maxAttempts = 20;

    const tryScroll = () => {
      if (scrollToHash()) return;

      attempts++;
      if (attempts < maxAttempts) {
        setTimeout(tryScroll, 50);
      }
    };

    tryScroll();
  }, [selectedPageContent]);

  // Convert HTML with pre blocks into JSX with CodeBlock components
  const decodeHtmlEntities = (text) => {
    const textarea = document.createElement("textarea");
    textarea.innerHTML = text;
    return textarea.value;
  };

  const extractCodeFromFragment = (fragmentHtml) => {
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
      lineClone.querySelectorAll('span.lineno, a[id^="l"], a[name^="l"]').forEach((el) => el.remove());

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
  };

  const renderContentWithCodeBlocks = (html, isSourcePage = false) => {
    const elements = [];
    let remaining = html;
    let position = 0;

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
        codeContent = decodeHtmlEntities(codeContent);
        if (codeContent.trim()) {
          const lines = codeContent.split("\n");
          const maxLineNum = lines.length.toString().length;

          // Extract target line number(s) from hash if present
          const hash = window.location.hash;
          let targetLineStart = null;
          let targetLineEnd = null;

          if (isSourcePage) {
            // Match both single line (#l00423) and range (#l00423-00458)
            const rangeMatch = hash.match(/^#l(\d+)(?:-(\d+))?$/);
            if (rangeMatch) {
              targetLineStart = parseInt(rangeMatch[1], 10);
              targetLineEnd = rangeMatch[2] ? parseInt(rangeMatch[2], 10) : targetLineStart;
            }
          }

          // If target line(s) exist on source pages, render with yellow background and nice arrows
          if (targetLineStart !== null && isSourcePage) {
            const highlightedHtml = lines
              .map((text, idx) => {
                const lineNum = idx + 1;
                const paddedNum = String(lineNum).padStart(maxLineNum, " ");
                const isTarget = lineNum >= targetLineStart && lineNum <= targetLineEnd;

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
            // No target line, use CodeBlock for syntax highlighting
            const codeWithLineNumbers = lines
              .map((text, idx) => {
                const lineNum = idx + 1;
                const paddedNum = String(lineNum).padStart(maxLineNum, " ");
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
        let inTag = false;

        for (let i = fragmentStart; i < remaining.length; i++) {
          if (remaining[i] === "<") {
            inTag = true;
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
          // Calculate max line number width for alignment
          const maxLineNum = codeLines.length > 0
            ? Math.max(...codeLines.map((line) => (line.number || 0).toString().length))
            : 1;

          // Extract target line number(s) from hash if present
          const hash = window.location.hash;
          let targetLineStart = null;
          let targetLineEnd = null;

          if (isSourcePage) {
            // Match both single line (#l00423) and range (#l00423-00458)
            const rangeMatch = hash.match(/^#l(\d+)(?:-(\d+))?$/);
            if (rangeMatch) {
              targetLineStart = parseInt(rangeMatch[1], 10);
              targetLineEnd = rangeMatch[2] ? parseInt(rangeMatch[2], 10) : targetLineStart;

              // Only set if at least one line exists in the code
              if (!codeLines.some((line) => {
                const lineNum = line.number;
                return lineNum >= targetLineStart && lineNum <= targetLineEnd;
              })) {
                targetLineStart = null;
                targetLineEnd = null;
              }
            }
          }

          // Build code with arrows, then highlight after syntax highlighting
          const codeWithLineNumbers = codeLines
            .map((line) => {
              const lineNum = line.number || "";
              const paddedNum = String(lineNum).padStart(maxLineNum, " ");
              const isTarget = targetLineStart !== null &&
                               line.number >= targetLineStart &&
                               line.number <= targetLineEnd;

              if (isTarget) {
                return `⟹ ${paddedNum}  ${line.text} ⟸`;
              }
              return `    ${paddedNum}  ${line.text}`;
            })
            .join("\n");

          elements.push(
            <div key={`code-wrapper-${elements.length}`} className="code-with-highlight">
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

          // Add JavaScript to highlight the lines with arrows after CodeBlock renders
          if (targetLineStart !== null && isSourcePage) {
            setTimeout(() => {
              const codeBlocks = document.querySelectorAll(".code-with-highlight pre code");
              codeBlocks.forEach((block) => {
                const allSpans = Array.from(block.querySelectorAll("span"));

                allSpans.forEach((span, spanIdx) => {
                  if (span.textContent.includes("⟹")) {
                    // Found arrow, highlight spans until we hit a newline
                    let highlightCount = 0;

                    // Highlight this span (the arrow)
                    span.style.backgroundColor = "#fbbf24";
                    span.style.color = "#000";
                    highlightCount++;

                    // Highlight following spans until newline
                    for (let i = spanIdx + 1; i < allSpans.length; i++) {
                      const nextSpan = allSpans[i];
                      if (nextSpan.textContent.includes("\n")) {
                        // Stop at newline, but still highlight this span if it contains the newline
                        if (nextSpan.textContent.includes("⟸")) {
                          nextSpan.style.backgroundColor = "#fbbf24";
                          nextSpan.style.color = "#000";
                          highlightCount++;
                        }
                        break;
                      }
                      nextSpan.style.backgroundColor = "#fbbf24";
                      nextSpan.style.color = "#000";
                      highlightCount++;
                    }

                    console.log(`[highlight] Highlighted ${highlightCount} spans in logical line with arrows`);
                  }
                });
              });
            }, 0);
          }
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
  };

  return (
    <>
      <AsciiChatHead
        title="ascii-chat(3) - Library Functions | ascii-chat"
        description="C library function reference for ascii-chat. Complete API documentation with function signatures, data structures, and type definitions."
        url="https://ascii-chat.com/man3"
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col">
        <div className="flex-1 flex flex-col px-4 sm:px-6 py-8 sm:py-12 w-full">
          {/* Header */}
          <header className="mb-8 sm:mb-12">
            <h1 className="text-3xl sm:text-4xl md:text-5xl font-bold mb-4">
              <span className="text-purple-400">📚</span> ascii-chat-*(3)
            </h1>
            <p className="text-lg sm:text-xl text-gray-300 mb-6">
              C API documentation for libasciichat and ascii-chat executables
            </p>

            {/* Search Box */}
            <div className="w-full">
              <div className="flex items-center gap-4">
                <label className="text-lg font-medium text-gray-300 whitespace-nowrap">
                  Search:
                </label>
                <div className="relative flex-1">
                  <input
                    type="text"
                    placeholder="Search by name or regex (e.g., 'socket', '^asciichat_.*', 'error|crypto')..."
                    value={searchQuery}
                    onChange={(e) => setSearchQuery(e.target.value)}
                    className="w-full bg-gray-900 border border-gray-700 rounded-lg px-4 py-3 text-gray-100 placeholder-gray-500 focus:outline-none focus:border-purple-500 focus:ring-2 focus:ring-purple-500/20 transition-colors"
                  />
                  {searchQuery && (
                    <button
                      onClick={() => setSearchQuery("")}
                      className="absolute right-3 top-1/2 -translate-y-1/2 text-gray-500 hover:text-gray-300"
                    >
                      ✕
                    </button>
                  )}
                </div>
              </div>
              <p className="text-xs text-gray-500 mt-2">
                Standard regex syntax supported
              </p>
            </div>
            <p className="text-sm text-gray-400 mt-3 text-center">
              {searching
                ? "Searching..."
                : filesMatched > 0
                  ? `${filesMatched} file${filesMatched !== 1 ? "s" : ""} matched, ${totalMatches} match${totalMatches !== 1 ? "es" : ""}`
                  : "No results"}
            </p>
          </header>

          {/* Main content area */}
          <div className="flex flex-col lg:flex-row gap-8">
            {/* Results list */}
            <div className="lg:w-2/5 flex-shrink-0">
              <div className="bg-gray-900/50 border border-gray-800 rounded-lg overflow-y-auto max-h-[calc(100vh-300px)] sticky top-20">
                {loading ? (
                  <div className="p-4 text-center text-gray-400">
                    Loading pages...
                  </div>
                ) : searchResults.length === 0 ? (
                  <div className="p-4 text-center text-gray-400">
                    {searchQuery ? "No pages found" : "Search to get started"}
                  </div>
                ) : (
                  <div className="divide-y divide-gray-800">
                    {searchResults.map((page) => (
                      <div
                        key={page.name}
                        className={`border-l-4 transition-colors ${
                          selectedPageName === page.name
                            ? "bg-purple-900/30 border-purple-500"
                            : "border-gray-800 hover:bg-gray-800/50"
                        }`}
                      >
                        <button
                          onClick={() => loadPageContent(page.name)}
                          className="w-full text-left px-4 py-3 block cursor-pointer hover:text-purple-200 transition-colors"
                        >
                          <div className="font-mono text-sm font-semibold truncate text-purple-300">
                            {highlightMatches(page.name, searchQuery)}
                          </div>
                          <div className="text-xs text-gray-400 mt-1 line-clamp-2">
                            {highlightMatches(
                              page.title || page.name,
                              searchQuery,
                            )}
                          </div>
                        </button>

                        {/* Snippets in nested boxes */}
                        {page.snippets && page.snippets.length > 0 && (
                          <div className="px-4 pb-3 space-y-2">
                            {page.snippets.map((snippet, idx) => {
                              const snippetLines = snippet.text.split("\n");
                              const [
                                beforeLineNum,
                                matchLineNum,
                                afterLineNum,
                              ] = snippet.lineNumbers;
                              return (
                                <div
                                  key={idx}
                                  onClick={() =>
                                    loadPageContent(
                                      page.name,
                                      snippet.lineNumbers[1],
                                      idx,
                                    )
                                  }
                                  className="bg-gray-950/80 border border-gray-700/50 rounded px-2 py-2 text-xs text-gray-300 font-mono overflow-hidden cursor-pointer hover:bg-gray-900/80 hover:border-gray-600/50 transition-colors"
                                >
                                  <div className="flex gap-2">
                                    {/* Line numbers column */}
                                    <div className="text-gray-600 text-right flex-shrink-0 select-none">
                                      {snippet.lineNumbers.map(
                                        (lineNum, lineIdx) => (
                                          <div key={lineIdx}>{lineNum}</div>
                                        ),
                                      )}
                                    </div>
                                    {/* Code content */}
                                    <div className="whitespace-pre-wrap break-words flex-1">
                                      {snippetLines.map((line, lineIdx) => (
                                        <div
                                          key={lineIdx}
                                          className={
                                            lineIdx ===
                                            Math.floor(snippetLines.length / 2)
                                              ? "bg-gray-800/50 px-1 -mx-1"
                                              : ""
                                          }
                                        >
                                          {highlightMatches(line, searchQuery)}
                                        </div>
                                      ))}
                                    </div>
                                  </div>
                                </div>
                              );
                            })}
                            {page.totalMatchesInFile > page.snippets.length && (
                              <div className="bg-yellow-900/50 border border-yellow-700/50 rounded px-3 py-2 text-sm font-semibold text-yellow-200">
                                ...{" "}
                                {page.totalMatchesInFile - page.snippets.length}{" "}
                                more matching result
                                {page.totalMatchesInFile -
                                  page.snippets.length !==
                                1
                                  ? "s"
                                  : ""}{" "}
                                for this file
                              </div>
                            )}
                          </div>
                        )}
                      </div>
                    ))}
                  </div>
                )}
              </div>
            </div>

            {/* Content viewer */}
            <div className="flex-1 min-w-0">
              {selectedPageContent ? (
                <div
                  ref={contentViewerRef}
                  className="bg-gray-900/30 border border-gray-800 rounded-lg p-6 overflow-y-auto max-h-[calc(100vh-300px)]"
                >
                  <div className="flex items-center justify-between mb-4 pb-4 border-b border-gray-800">
                    <h2 className="text-2xl font-bold text-purple-400">
                      {selectedPageName}(3)
                    </h2>
                    <button
                      onClick={() => {
                        setSelectedPageContent(null);
                        setSelectedPageName(null);
                      }}
                      className="text-gray-500 hover:text-gray-300"
                    >
                      ✕
                    </button>
                  </div>
                  <div className="man-page-content">
                    {renderContentWithCodeBlocks(
                      selectedPageContent,
                      selectedPageName?.endsWith("_source") || false
                    )}
                  </div>
                </div>
              ) : (
                <div className="bg-gray-900/30 border border-gray-800 rounded-lg p-12 flex items-center justify-center min-h-96 text-center">
                  <div>
                    <p className="text-gray-400 text-lg mb-2">
                      Select a page to view documentation
                    </p>
                    <p className="text-gray-500 text-sm">
                      {searchResults.length > 0
                        ? "Click any page name in the list"
                        : "Search to find API documentation"}
                    </p>
                  </div>
                </div>
              )}
            </div>
          </div>

          {/* Footer */}
          <div className="mt-16">
            <Footer />
          </div>
        </div>
      </div>
    </>
  );
}
