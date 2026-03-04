import { useState, useEffect, useRef } from "react";
import Footer from "../components/Footer";
import { setBreadcrumbSchema } from "../utils/breadcrumbs";
import { AsciiChatHead } from "../components/AsciiChatHead";

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
      fetch(`/man3/${pageName}.html`)
        .then((r) => r.text())
        .then((html) => {
          const bodyMatch = html.match(/<body[^>]*>([\s\S]*)<\/body>/i);
          const content = bodyMatch ? bodyMatch[1] : html;
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
        // Clear URL param when search is empty
        window.history.replaceState({}, "", "/man3");
        return;
      }

      setSearching(true);

      try {
        const response = await fetch(
          `/api/man3/search?q=${encodeURIComponent(searchQuery)}`
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

        // Update URL with search query
        const newUrl = `/man3?q=${encodeURIComponent(searchQuery)}`;
        window.history.replaceState({}, "", newUrl);
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

  const loadPageContent = (pageName, lineNumber = null, snippetIndex = null) => {
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
        const bodyMatch = html.match(/<body[^>]*>([\s\S]*)<\/body>/i);
        const content = bodyMatch ? bodyMatch[1] : html;
        // Highlight matches in the HTML content
        const highlightedContent = highlightMatchesInHTML(content, searchQuery);
        setSelectedPageContent(highlightedContent);
        setSelectedPageName(pageName);
        if (lineNumber) {
          setTargetLineNumber(lineNumber);
          setTargetSnippetIndex(snippetIndex);
        }
        // Update URL with selected page param
        const params = new URLSearchParams(window.location.search);
        params.set("page", pageName);
        window.history.replaceState({}, "", `/man3?${params.toString()}`);
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

      const highlighted = parts.map(part => {
        if (part.startsWith('<')) {
          // It's a tag, don't modify
          return part;
        } else {
          // It's text content, highlight matches
          return part.replace(
            regex,
            '<span class="bg-yellow-900/50 text-yellow-200">$1</span>'
          );
        }
      }).join('');

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
          const highlights = viewer.querySelectorAll('.man-page-content span.bg-yellow-900\\/50');

          // Use snippet index if available, otherwise find first match near target line
          let targetHighlight = null;
          if (targetSnippetIndex !== null && highlights.length > targetSnippetIndex) {
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
                <label className="text-lg font-medium text-gray-300 whitespace-nowrap">Search:</label>
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
              <p className="text-xs text-gray-500 mt-2">Standard regex syntax supported</p>
            </div>
            <p className="text-sm text-gray-400 mt-3 text-center">
              {searching ? (
                "Searching..."
              ) : filesMatched > 0 ? (
                `${filesMatched} file${filesMatched !== 1 ? "s" : ""} matched, ${totalMatches} match${totalMatches !== 1 ? "es" : ""}`
              ) : (
                "No results"
              )}
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
                              searchQuery
                            )}
                          </div>
                        </button>

                        {/* Snippets in nested boxes */}
                        {page.snippets && page.snippets.length > 0 && (
                          <div className="px-4 pb-3 space-y-2">
                            {page.snippets.map((snippet, idx) => {
                              const snippetLines = snippet.text.split("\n");
                              const [beforeLineNum, matchLineNum, afterLineNum] = snippet.lineNumbers;
                              return (
                                <div
                                  key={idx}
                                  onClick={() => loadPageContent(page.name, snippet.lineNumbers[1], idx)}
                                  className="bg-gray-950/80 border border-gray-700/50 rounded px-2 py-2 text-xs text-gray-300 font-mono overflow-hidden cursor-pointer hover:bg-gray-900/80 hover:border-gray-600/50 transition-colors"
                                >
                                  <div className="flex gap-2">
                                    {/* Line numbers column */}
                                    <div className="text-gray-600 text-right flex-shrink-0 select-none">
                                      {snippet.lineNumbers.map((lineNum, lineIdx) => (
                                        <div key={lineIdx}>
                                          {lineNum}
                                        </div>
                                      ))}
                                    </div>
                                    {/* Code content */}
                                    <div className="whitespace-pre-wrap break-words flex-1">
                                      {snippetLines.map((line, lineIdx) => (
                                        <div
                                          key={lineIdx}
                                          className={
                                            lineIdx === Math.floor(snippetLines.length / 2)
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
                                ... {page.totalMatchesInFile - page.snippets.length} more matching result{page.totalMatchesInFile - page.snippets.length !== 1 ? "s" : ""} for this file
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
                <div ref={contentViewerRef} className="bg-gray-900/30 border border-gray-800 rounded-lg p-6 overflow-y-auto max-h-[calc(100vh-300px)]">
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
                  <div
                    className="man-page-content"
                    dangerouslySetInnerHTML={{ __html: selectedPageContent }}
                  />
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
