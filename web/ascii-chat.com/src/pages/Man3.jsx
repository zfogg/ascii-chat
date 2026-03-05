import { useState, useEffect, useRef, useMemo, useCallback } from "react";
import Footer from "../components/Footer";
import { setBreadcrumbSchema } from "../utils/breadcrumbs";
import { AsciiChatHead } from "../components/AsciiChatHead";
import { CodeBlock } from "@ascii-chat/shared/components";
import "../styles/man.css";

export default function Man3() {
  const [manPages, setManPages] = useState([]);
  const [searchQuery, setSearchQuery] = useState("");
  const [searchResults, setSearchResults] = useState([]);
  const [filesMatched, setFilesMatched] = useState(0);
  const [totalMatches, setTotalMatches] = useState(0);
  const [moreFilesCount, setMoreFilesCount] = useState(0);
  const [loading, setLoading] = useState(true);
  const [searching, setSearching] = useState(false);
  const [selectedPageContent, setSelectedPageContent] = useState(null);
  const [selectedPageName, setSelectedPageName] = useState(null);
  const [pageNotFound, setPageNotFound] = useState(false);
  const [targetLineNumber, setTargetLineNumber] = useState(null);
  const [targetSnippetIndex, setTargetSnippetIndex] = useState(null);
  const [regexError, setRegexError] = useState(null);
  const searchTimeoutRef = useRef(null);
  const contentViewerRef = useRef(null);

  // Helper function to process HTML content: convert URLs and highlight matches
  const processPageContent = useCallback((html, searchQuery) => {
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
      /href="(?:https?:\/\/[^/]+)?([^"]*\/)?([^/".]+\.html)(#l\d+)?"/gi,
      (match, path, htmlFile, anchor) => {
        const newPageName = htmlFile.replace(".html", "");
        const newHref = `/man3?page=${newPageName}${anchor || ""}`;
        return `href="${newHref}"`;
      },
    );

    // Preserve empty anchor tags by adding zero-width space
    content = content.replace(
      /<a\s+id="(l\d+)"[^>]*>\s*<\/a>/g,
      '<a id="$1">\u200B</a>',
    );

    // Highlight matches in the processed content
    return highlightMatchesInHTML(content, searchQuery);
  }, []);

  // Helper function to add GitHub links to "Definition at line X of file Y"
  // Also converts line number anchors on source pages to GitHub links
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

      // If manPages is loaded, use it to find the actual filename
      // Otherwise, fetch pages.json first
      if (manPages.length > 0) {
        const page = manPages.find((p) => p.name === pageName);

        // Check if page exists in the pages list
        if (!page) {
          setPageNotFound(true);
          setSelectedPageContent(null);
        } else {
          setPageNotFound(false);
          const filename = page.file;

          fetch(`/man3/${filename}`)
            .then((r) => {
              if (!r.ok) {
                setPageNotFound(true);
                setSelectedPageContent(null);
                return null;
              }
              return r.text();
            })
            .then((html) => {
              if (!html) return;
              setPageNotFound(false);
              // Extract stylesheets from head
              const stylesheets = [];
              const styleMatches = html.matchAll(
                /<link[^>]*rel="stylesheet"[^>]*href="([^"]+)"[^>]*>/gi,
              );
              for (const match of styleMatches) {
                const href = match[1];
                if (!href.startsWith("/") && !href.startsWith("http")) {
                  stylesheets.push(
                    `<link rel="stylesheet" href="/man3/${href}" />`,
                  );
                } else {
                  stylesheets.push(match[0]);
                }
              }

              // Process content (URLs and highlighting)
              // Use query param from URL if available for highlighting search results
              const decodedQuery = queryParam ? decodeURIComponent(queryParam) : "";
              let processedContent = processPageContent(html, decodedQuery);

              // Add GitHub links for "Definition at line X" text and line numbers
              const page = manPages.find((p) => p.name === pageName);
              const sourcePath = page?.sourcePath;
              const isSourcePage = pageName?.endsWith("_source") || false;
              processedContent = processDefinitionLinks(
                processedContent,
                sourcePath,
                __COMMIT_SHA__,
                isSourcePage,
              );

              // Prepend stylesheets
              const content = stylesheets.join("\n") + processedContent;
              setSelectedPageContent(content);

              // If there's a hash indicating a line number, extract and set it for scrolling
              const hash = window.location.hash.substring(1);
              if (hash.match(/^l\d+$/)) {
                const lineNum = parseInt(hash.substring(1), 10);
                setTargetLineNumber(lineNum);
              }
            })
            .catch((e) => {
              console.error("Failed to load page:", e);
              setPageNotFound(true);
              setSelectedPageContent(null);
            });
        }
      } else {
        // manPages not loaded yet, try default filename
        fetch(`/man3/${pageName}.html`)
          .then((r) => {
            if (!r.ok) {
              setPageNotFound(true);
              setSelectedPageContent(null);
              return null;
            }
            return r.text();
          })
          .then((html) => {
            if (!html) return;
            setPageNotFound(false);
            // Extract stylesheets from head
            const stylesheets = [];
            const styleMatches = html.matchAll(
              /<link[^>]*rel="stylesheet"[^>]*href="([^"]+)"[^>]*>/gi,
            );
            for (const match of styleMatches) {
              const href = match[1];
              if (!href.startsWith("/") && !href.startsWith("http")) {
                stylesheets.push(
                  `<link rel="stylesheet" href="/man3/${href}" />`,
                );
              } else {
                stylesheets.push(match[0]);
              }
            }

            // Process content (URLs and highlighting)
            // Use query param from URL if available for highlighting search results
            const decodedQuery = queryParam ? decodeURIComponent(queryParam) : "";
            let processedContent = processPageContent(html, decodedQuery);

            // Add GitHub links for "Definition at line X" text and line numbers
            const page = manPages.find((p) => p.name === pageName);
            const sourcePath = page?.sourcePath;
            const isSourcePage = pageName?.endsWith("_source") || false;
            processedContent = processDefinitionLinks(
              processedContent,
              sourcePath,
              __COMMIT_SHA__,
              isSourcePage,
            );

            // Prepend stylesheets
            const content = stylesheets.join("\n") + processedContent;
            setSelectedPageContent(content);

            // If there's a hash indicating a line number, extract and set it for scrolling
            const hash = window.location.hash.substring(1);
            if (hash.match(/^l\d+$/)) {
              const lineNum = parseInt(hash.substring(1), 10);
              setTargetLineNumber(lineNum);
            }
          })
          .catch((e) => {
            console.error("Failed to load page:", e);
            setPageNotFound(true);
            setSelectedPageContent(null);
          });
      }
    }
  }, [
    processPageContent,
    processDefinitionLinks,
    manPages,
    window.location.search,
  ]);

  // Load man3 index
  useEffect(() => {
    fetch("/man3/pages.json")
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

  // Debounced search function
  const performSearch = useCallback(
    async (query) => {
      if (!query.trim()) {
        setSearchResults(manPages);
        setFilesMatched(0);
        setTotalMatches(0);
        setMoreFilesCount(0);
        setSearching(false);
        setRegexError(null);
        // Clear search param but preserve page param if present
        const params = new URLSearchParams(window.location.search);
        params.delete("q");
        const newUrl = params.toString()
          ? `/man3?${params.toString()}`
          : "/man3";
        window.history.replaceState({}, "", newUrl + window.location.hash);
        return;
      }

      // Validate regex syntax
      try {
        const regexMatch = query.match(/^\/(.+)\/([gimuy]*)$/);
        if (regexMatch) {
          new RegExp(regexMatch[1], regexMatch[2] || "i");
        } else {
          // Try as literal string with i flag
          new RegExp(query.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), "i");
        }
        setRegexError(null);
      } catch (e) {
        setRegexError(e.message);
        setSearchResults([]);
        setFilesMatched(0);
        setTotalMatches(0);
        setMoreFilesCount(0);
        return;
      }

      try {
        const response = await fetch(
          `/api/man3/search?q=${encodeURIComponent(query)}`,
        );
        const data = await response.json();

        if (data.error) {
          setSearchResults([]);
          setFilesMatched(0);
          setTotalMatches(0);
          setMoreFilesCount(0);
        } else {
          setSearchResults(data.results || []);
          setFilesMatched(data.filesMatched || 0);
          setTotalMatches(data.totalMatches || 0);
          setMoreFilesCount(data.moreFilesCount || 0);
        }

        // Update URL with search query (preserve page param if present)
        const params = new URLSearchParams(window.location.search);
        params.set("q", query);
        window.history.replaceState(
          {},
          "",
          `/man3?${params.toString()}` + window.location.hash,
        );
      } catch (e) {
        console.error("Search error:", e);
        setSearchResults([]);
        setFilesMatched(0);
        setTotalMatches(0);
        setMoreFilesCount(0);
      } finally {
        setSearching(false);
      }
    },
    [manPages],
  );

  // Debounce the API call when search query changes
  useEffect(() => {
    if (searchTimeoutRef.current) {
      clearTimeout(searchTimeoutRef.current);
    }

    if (!searchQuery.trim()) {
      performSearch("");
      return;
    }

    setSearching(true);
    searchTimeoutRef.current = setTimeout(() => {
      performSearch(searchQuery);
    }, 500);

    return () => {
      if (searchTimeoutRef.current) {
        clearTimeout(searchTimeoutRef.current);
      }
    };
  }, [searchQuery, manPages, performSearch]);

  const loadPageContent = useCallback(
    (
      pageName,
      lineNumber = null,
      snippetIndex = null,
      skipHistoryPush = false,
    ) => {
      // If clicking a line number on the same page, just update the hash without fetching
      if (selectedPageName === pageName && lineNumber !== null) {
        setTargetLineNumber(lineNumber);
        setTargetSnippetIndex(snippetIndex);
        const hash = "#l" + lineNumber.toString().padStart(5, "0");
        window.history.replaceState(
          {},
          "",
          window.location.pathname + window.location.search + hash,
        );
        return;
      }

      // Look up the actual filename from manPages
      // Falls back to ${pageName}.html if page not found
      let filename = `${pageName}.html`;
      const page = manPages.find((p) => p.name === pageName);
      if (page) {
        filename = page.file;
      }

      fetch(`/man3/${filename}`)
        .then((r) => r.text())
        .then((html) => {
          // Scroll to top before loading new content
          // Only reset scroll if there's no hash anchor (hash will handle scrolling)
          if (contentViewerRef.current && !window.location.hash) {
            contentViewerRef.current.scrollTop = 0;
          }
          // Read current search query from URL to ensure we use latest value
          const currentParams = new URLSearchParams(window.location.search);
          const currentSearchQuery = currentParams.get("q") || searchQuery;
          let processedContent = processPageContent(html, currentSearchQuery);

          // Add GitHub links for "Definition at line X" text and line numbers
          const page = manPages.find((p) => p.name === pageName);
          const sourcePath = page?.sourcePath;
          const isSourcePage = pageName?.endsWith("_source") || false;
          processedContent = processDefinitionLinks(
            processedContent,
            sourcePath,
            __COMMIT_SHA__,
            isSourcePage,
          );

          setSelectedPageContent(processedContent);
          setSelectedPageName(pageName);
          if (lineNumber) {
            setTargetLineNumber(lineNumber);
            setTargetSnippetIndex(snippetIndex);
          }
          // Update URL with selected page param and preserve search query
          const params = new URLSearchParams(window.location.search);
          params.set("page", pageName);
          // Ensure search query is preserved in URL
          if (searchQuery && !params.has("q")) {
            params.set("q", searchQuery);
          }
          // Set hash based on lineNumber: if provided, use it; if not, preserve existing hash (unless it's a line number hash)
          let hash = "";
          if (lineNumber) {
            hash = "#l" + lineNumber.toString().padStart(5, "0");
          } else {
            // Only remove line number hashes (#l0000 or #l0000-00000) when not targeting a line
            // Preserve any other hashes
            const currentHash = window.location.hash;
            if (currentHash && !/#l\d+(?:-\d+)?$/.test(currentHash)) {
              hash = currentHash;
            }
          }
          if (!skipHistoryPush) {
            window.history.pushState(
              {},
              "",
              `/man3?${params.toString()}` + hash,
            );
          }
        })
        .catch((e) => console.error("Failed to load page:", e));
    },
    [processPageContent, searchQuery, selectedPageName, manPages],
  );

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
    } catch (_e) {
      return text;
    }
  };

  // Memoize highlighted search results to avoid expensive rendering during typing
  const highlightedResults = useMemo(
    () =>
      searchResults.map((page) => ({
        ...page,
        highlightedName: highlightMatches(page.name, searchQuery),
        highlightedTitle: highlightMatches(
          page.title || page.name,
          searchQuery,
        ),
      })),
    [searchResults, searchQuery],
  );

  const highlightMatchesInHTML = (html, query) => {
    if (!query.trim()) return html;

    try {
      const regex = new RegExp(`(${query})`, "gi");
      // For code blocks: preserve Doxygen syntax highlighting while adding search highlight
      // Wrap matches with highlight span but preserve existing span classes
      return html.replace(regex, '<span class="bg-yellow-900/50 text-yellow-200">$1</span>');
    } catch (_e) {
      return html;
    }
  };

  // Scroll search result snippet to center when selected
  useEffect(() => {
    if (
      targetSnippetIndex !== null &&
      selectedPageName &&
      searchResults.length > 0
    ) {
      // Find the snippet element in the left panel and scroll it into view
      setTimeout(() => {
        // Create unique ID: page-name + snippet-index
        const snippetId = `${selectedPageName}-${targetSnippetIndex}`;
        const targetElement = document.querySelector(
          `[data-snippet-id="${snippetId}"]`,
        );

        if (targetElement) {
          // Find the scrollable parent (the results list container)
          const scrollParent = targetElement.closest(
            '.overflow-y-auto[class*="max-h"]',
          );

          if (scrollParent) {
            // Calculate scroll position to center the element
            const elementTop = targetElement.offsetTop;
            const elementHeight = targetElement.clientHeight;
            const scrollParentHeight = scrollParent.clientHeight;

            // Center the element in the viewport
            const scrollTop = Math.max(
              0,
              elementTop + elementHeight / 2 - scrollParentHeight / 2
            );

            scrollParent.scrollTop = scrollTop;
          }
        }
      }, 100);
    }
  }, [targetSnippetIndex, selectedPageName, searchResults]);

  // Auto-load first search result when search results appear
  useEffect(() => {
    if (searchResults.length > 0 && !selectedPageName && searchQuery) {
      // Load first search result's page
      const pageName = searchResults[0].name;
      const filename = searchResults[0].file || `${pageName}.html`;

      fetch(`/man3/${filename}`)
        .then((r) => r.text())
        .then((html) => {
          let processedContent = processPageContent(html, searchQuery);
          const page = searchResults[0];
          const sourcePath = page?.sourcePath;
          const isSourcePage = pageName?.endsWith("_source") || false;
          processedContent = processDefinitionLinks(
            processedContent,
            sourcePath,
            __COMMIT_SHA__,
            isSourcePage,
          );
          setSelectedPageContent(processedContent);
          setSelectedPageName(pageName);

          // Update URL with selected page param
          const params = new URLSearchParams(window.location.search);
          params.set("page", pageName);
          window.history.replaceState(
            {},
            "",
            `/man3?${params.toString()}`,
          );
        })
        .catch((e) => console.error("Failed to load first search result:", e));
    }
  }, [searchResults, searchQuery, processPageContent, processDefinitionLinks]);

  // Scroll to target line when it's set
  useEffect(() => {
    if (!selectedPageContent || !contentViewerRef.current) return;

    const viewer = contentViewerRef.current;

    // Delay to allow content to render
    setTimeout(() => {
      // Clear any previous yellow highlighting
      const allHighlighted = viewer.querySelectorAll('[style*="background-color"]');
      for (const el of allHighlighted) {
        if (el.style.backgroundColor === "rgb(255, 191, 36)" || el.style.backgroundColor === "#fbbf24") {
          el.style.backgroundColor = "";
        }
      }

      // Scroll to target line if set (when clicking search snippets)
      if (targetLineNumber) {
        console.log("[Man3] Looking for line:", targetLineNumber);

        // Search for any element that contains the target line number
        const walker = document.createTreeWalker(
          viewer,
          NodeFilter.SHOW_TEXT,
          null,
          false
        );

        let foundElement = null;
        let node;

        // Walk through all text nodes looking for the line number
        while ((node = walker.nextNode())) {
          const text = node.textContent || "";
          // Look for text that starts with the line number followed by whitespace or special chars
          const match = text.match(new RegExp(`\\b${targetLineNumber}\\b`));
          if (match) {
            console.log("[Man3] Found text node with line number:", text.substring(0, 50));
            // Found a text node containing the line number
            // Get the closest parent element (which should be a visible container)
            foundElement = node.parentElement;
            console.log("[Man3] foundElement tag:", foundElement?.tagName, "height:", foundElement?.offsetHeight);

            // Walk up to find a reasonable scrollable/visible parent
            let parent = foundElement;
            while (parent && parent !== viewer && parent.offsetHeight === 0) {
              parent = parent.parentElement;
            }
            if (parent && parent !== viewer) {
              foundElement = parent;
              console.log("[Man3] After walking up from 0-height, foundElement:", foundElement?.tagName, "height:", foundElement?.offsetHeight);
            }
            break;
          }
        }

        if (foundElement) {
          console.log("[Man3] foundElement:", foundElement?.tagName, "height:", foundElement?.offsetHeight, "offsetTop:", foundElement?.offsetTop);

          // Get all spans in the CODE block and find those on the same line
          const codeBlock = foundElement.closest("code") || foundElement.closest("pre");
          console.log("[Man3] Code block:", codeBlock?.tagName);

          if (codeBlock) {
            // Find the offsetTop of our element to identify its line
            const targetTop = foundElement.offsetTop;
            console.log("[Man3] Target line offsetTop:", targetTop);

            // Find all spans/elements in the code block
            const allSpans = codeBlock.querySelectorAll("span, div, *");
            const lineElements = [];

            // Collect all spans on this line that have non-whitespace content
            const lineSpans = [];
            for (const span of allSpans) {
              if (span.offsetTop === targetTop && span.offsetHeight > 0 && span.offsetHeight < 100) {
                const text = span.textContent || "";
                if (text.trim().length > 0) {
                  lineSpans.push(span);
                }
              }
            }

            console.log("[Man3] Found", lineSpans.length, "non-empty spans on line");

            // Sort by offsetLeft to process left-to-right, then by DOM order
            lineSpans.sort((a, b) => {
              if (Math.abs(a.offsetLeft - b.offsetLeft) > 1) {
                return a.offsetLeft - b.offsetLeft;
              }
              // Same position - preserve DOM order
              return 0;
            });

            // Filter: only keep spans that have minimal overlap
            // Two spans overlap if their offsetLeft values are very close (within 5px)
            // Keep only the first span at each visual position cluster
            const spansToHighlight = [];
            const positionClusters = []; // Track positions we've already covered

            for (const span of lineSpans) {
              let overlapsExisting = false;

              // Check if this span's position already has a nearby span highlighted
              for (const clusterPos of positionClusters) {
                if (Math.abs(span.offsetLeft - clusterPos) < 5) {
                  overlapsExisting = true;
                  break;
                }
              }

              if (!overlapsExisting) {
                spansToHighlight.push(span);
                positionClusters.push(span.offsetLeft);
                console.log("[Man3] Selected span at", span.offsetLeft, "text:", span.textContent?.substring(0, 20));
              } else {
                console.log("[Man3] Skipped span at", span.offsetLeft, "(too close to existing)", "text:", span.textContent?.substring(0, 20));
              }
            }

            console.log("[Man3] Will highlight", spansToHighlight.length, "spans");

            if (spansToHighlight.length > 0) {
              for (const el of spansToHighlight) {
                el.style.backgroundColor = "#fbbf24";
                el.style.color = "#000000";
                if (el.offsetHeight === 0) {
                  el.style.display = "inline";
                  el.style.visibility = "visible";
                  el.style.opacity = "1";
                }
              }

              console.log("[Man3] Applied highlight to", spansToHighlight.length, "elements");
              spansToHighlight[0].scrollIntoView({ behavior: 'auto', block: 'center' });
              console.log("[Man3] Scrolled line into view");
            }
          }
        } else {
          console.log("[Man3] No element found for line number");
        }
      }
    }, 100);
  }, [selectedPageContent, targetLineNumber]);

  // Handle Doxygen link interception and line number scrolling
  useEffect(() => {
    const viewer = contentViewerRef.current;
    if (!viewer) return;

    const handleLinkClick = (e) => {
      const link = e.target.closest("a");
      if (!link) return;

      const href = link.getAttribute("href");
      if (!href) return;


      // Match new converted format: /man3?page=filename[#l00123 or %23l00123]
      const newMatch = href.match(
        /^\/man3\?page=([^&#%]+)(?:%23|#)?(l\d+(?:-\d+)?)?/,
      );
      if (newMatch) {
        e.preventDefault();
        const pageName = decodeURIComponent(newMatch[1]);
        let lineNumber = null;
        if (newMatch[2]) {
          // Extract number from "l00046" format
          const numStr = newMatch[2].replace(/^l/, "").split("-")[0];
          lineNumber = parseInt(numStr, 10);
        }
        loadPageContent(pageName, lineNumber);
        return;
      }

      // Also match old Doxygen source file links for backward compatibility: /man3/filename.html#l00123
      const doxygenMatch = href.match(/\/man3\/(.+?)\.html(#l\d+)?$/);
      if (doxygenMatch) {
        e.preventDefault();
        const pageName = doxygenMatch[1];
        const lineAnchor = doxygenMatch[2] || "";

        // Load the page and scroll to line if specified
        loadPageContent(pageName);

        // If there's a line anchor, scroll to it after content loads
        if (lineAnchor) {
          setTimeout(() => {
            if (viewer) {
              const element = viewer.querySelector(lineAnchor);
              if (element) {
                element.scrollIntoView({ behavior: "smooth" });
              }
            }
          }, 300);
        }
      }
    };

    viewer.addEventListener("click", handleLinkClick);
    return () => {
      viewer.removeEventListener("click", handleLinkClick);
    };
  }, [loadPageContent]);

  // Scroll to hash fragment when page content changes
  useEffect(() => {
    if (!selectedPageContent || !contentViewerRef.current) return;

    const hash = window.location.hash;
    if (!hash || !hash.match(/#l(\d+)/)) return;

    // Try to find and scroll to code block matching the hash line number
    const scrollToHash = () => {
      const container = contentViewerRef.current;

      // Extract line number from hash
      const lineMatch = hash.match(/#l(\d+)/);
      if (!lineMatch) return false;

      const targetLineNum = parseInt(lineMatch[1], 10);

      // First try to find by exact ID match (for inline code blocks)
      const blockId = `code-block-${targetLineNum}`;
      let targetBlock = container.querySelector(`div[id="${blockId}"]`);
      if (targetBlock) {
        targetBlock.scrollIntoView({ behavior: "smooth", block: "center" });
        return true;
      }

      // Otherwise, look for a code block that contains this line number
      // by checking data attributes (first-line and last-line)
      const allCodeBlocks = container.querySelectorAll(".code-with-highlight");
      for (const block of allCodeBlocks) {
        const firstLine = parseInt(block.getAttribute("data-first-line"), 10);
        const lastLine = parseInt(block.getAttribute("data-last-line"), 10);

        if (!isNaN(firstLine) && !isNaN(lastLine) &&
            targetLineNum >= firstLine && targetLineNum <= lastLine) {
          // Found the code block that contains the target line
          block.scrollIntoView({ behavior: "smooth", block: "center" });
          return true;
        }
      }

      // Final fallback: scroll to first code block if nothing else matched
      const firstCodeBlock = container.querySelector(".code-with-highlight, pre");
      if (firstCodeBlock) {
        firstCodeBlock.scrollIntoView({ behavior: "smooth", block: "center" });
        return true;
      }

      return false;
    };

    // Try multiple times in case elements are still rendering
    let attempts = 0;
    const maxAttempts = 20;

    const tryScroll = () => {
      if (scrollToHash()) {
        return;
      }

      attempts++;
      if (attempts < maxAttempts) {
        setTimeout(tryScroll, 50);
      }
    };

    tryScroll();
  }, [selectedPageContent, window.location.hash]);

  // Handle browser back/forward navigation
  useEffect(() => {
    const handlePopState = () => {
      const params = new URLSearchParams(window.location.search);
      const pageName = params.get("page");

      if (pageName) {
        // Extract line number from hash if present
        const hash = window.location.hash;
        let lineNumber = null;
        const lineMatch = hash.match(/#l(\d+)/);
        if (lineMatch) {
          lineNumber = parseInt(lineMatch[1], 10);
        }
        // Skip pushState since history is already being managed by the back button
        loadPageContent(pageName, lineNumber, null, true);
      } else {
        // No page param, clear the page
        setSelectedPageName(null);
        setSelectedPageContent(null);
        setTargetLineNumber(null);
      }
    };

    window.addEventListener("popstate", handlePopState);
    return () => window.removeEventListener("popstate", handlePopState);
  }, [loadPageContent]);

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
      lineClone
        .querySelectorAll('span.lineno, a[id^="l"], a[name^="l"]')
        .forEach((el) => el.remove());

      // Unwrap fold divs in the clone
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

  const renderContentWithCodeBlocks = (html, isSourcePage = false, searchQuery = "", targetLineNum = null) => {
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
          .replace(/<span[^>]*style="[^"]*background-color:\s*#fbbf24[^"]*"[^>]*>/g, "")
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
          const maxLineNum = lines.length.toString().length;

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

          const codeWithLineNumbers = lines
            .map((text, idx) => {
              const lineNum = idx + 1;
              const paddedNum = String(lineNum).padStart(maxLineNum, " ");
              return `    ${paddedNum}  ${text}`;
            })
            .join("\n");

          // Use CodeBlock with line highlighting
          // Add an id to the container if this block has a target line for scrolling
          const blockId = targetLineStart ? `code-block-${targetLineStart}` : undefined;

          elements.push(
            <div
              key={`code-${elements.length}`}
              id={blockId}
              className="code-with-highlight"
              data-first-line={firstSourceLineNum}
              data-last-line={lastSourceLineNum}
            >
              <CodeBlock
                highlightLines={targetLineStart ? { start: targetLineStart, end: targetLineEnd } : undefined}
                searchQuery={searchQuery}
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
          // Calculate max line number width for alignment
          const maxLineNum =
            codeLines.length > 0
              ? Math.max(
                  ...codeLines.map(
                    (line) => (line.number || 0).toString().length,
                  ),
                )
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

          // Build code with arrows, then highlight after syntax highlighting
          const codeWithLineNumbers = codeLines
            .map((line) => {
              const lineNum = line.number || "";
              const paddedNum = String(lineNum).padStart(maxLineNum, " ");
              const isTarget =
                targetLineStart !== null &&
                line.number >= targetLineStart &&
                line.number <= targetLineEnd;

              if (isTarget) {
                return `⟹ ${paddedNum}  ${line.text} ⟸`;
              }
              return `    ${paddedNum}  ${line.text}`;
            })
            .join("\n");

          // Add an id to the wrapper if this block has a target line for scrolling
          const wrapperId = targetLineStart ? `code-block-${targetLineStart}` : undefined;
          // Also add data attribute to track which source lines are in this block
          const firstLineNum = codeLines.length > 0 ? codeLines[0].number : null;
          const lastLineNum = codeLines.length > 0 ? codeLines[codeLines.length - 1].number : null;

          elements.push(
            <div
              key={`code-wrapper-${elements.length}`}
              id={wrapperId}
              className="code-with-highlight"
              data-first-line={firstLineNum}
              data-last-line={lastLineNum}
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
              <CodeBlock searchQuery={searchQuery}>{codeWithLineNumbers}</CodeBlock>
            </div>,
          );

          // Add JavaScript to highlight the lines with arrows after CodeBlock renders
          if (targetLineStart !== null && isSourcePage) {
            setTimeout(() => {
              const codeBlocks = document.querySelectorAll(
                ".code-with-highlight pre code",
              );
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

                    console.log(
                      `[highlight] Highlighted ${highlightCount} spans in logical line with arrows`,
                    );
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
                    placeholder="Search by name or regex (e.g., 'socket' or /^asciichat_.*/ or /error|crypto/)..."
                    value={searchQuery}
                    onChange={(e) => setSearchQuery(e.target.value)}
                    className={`w-full border rounded-lg px-4 py-3 placeholder-gray-500 focus:outline-none transition-colors ${
                      regexError
                        ? "bg-red-900 border-red-500 text-white placeholder-red-200 focus:border-red-400 focus:ring-2 focus:ring-red-500/20"
                        : "bg-gray-900 border-gray-700 text-gray-100 focus:border-purple-500 focus:ring-2 focus:ring-purple-500/20"
                    }`}
                  />
                  {searchQuery && (
                    <button
                      onClick={() => setSearchQuery("")}
                      className={`absolute right-3 top-1/2 -translate-y-1/2 transition-colors ${
                        regexError
                          ? "text-red-200 hover:text-red-100"
                          : "text-gray-500 hover:text-gray-300"
                      }`}
                    >
                      ✕
                    </button>
                  )}
                </div>
              </div>
              {regexError && (
                <p className="text-sm text-red-400 mt-2 font-medium">
                  ⚠ Regex Error: {regexError}
                </p>
              )}
              {!regexError && (
                <div className="flex items-center justify-between gap-4 mt-2">
                  <p className="text-xs text-gray-500">
                    Regex search (default case-insensitive). Examples:{" "}
                    <code className="bg-gray-800 px-1 rounded">socket</code>,{" "}
                    <code className="bg-gray-800 px-1 rounded">
                      error|crypto
                    </code>
                    , or{" "}
                    <code className="bg-gray-800 px-1 rounded">
                      /^socket$/gi
                    </code>{" "}
                    for flags
                  </p>
                  <p className="text-xs text-gray-500 whitespace-nowrap">
                    📖{" "}
                    <a
                      href="https://zfogg.github.io/ascii-chat/"
                      target="_blank"
                      rel="noopener noreferrer"
                      className="text-cyan-400 hover:text-cyan-300 transition-colors"
                    >
                      View original Doxygen documentation
                    </a>
                  </p>
                </div>
              )}
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
                ) : searching && searchResults.length === 0 ? (
                  <div className="p-4 text-center text-blue-400">
                    Searching...
                  </div>
                ) : searchResults.length === 0 ? (
                  <div className="p-4 text-center text-gray-400">
                    {searchQuery ? "No pages found" : "Search to get started"}
                  </div>
                ) : (
                  <div className="divide-y divide-gray-800">
                    {highlightedResults.map((page, _index) => (
                      <div
                        key={`${page.file}-${page.name}`}
                        className={`border-l-4 transition-colors ${
                          selectedPageName === page.name
                            ? "bg-purple-900/30 border-purple-500"
                            : "border-gray-800 hover:bg-gray-800/50"
                        }`}
                      >
                        <button
                          onClick={() => {
                            window.location.hash = "";
                            loadPageContent(page.name);
                          }}
                          className="w-full text-left px-4 py-3 block cursor-pointer hover:text-purple-200 transition-colors"
                        >
                          <div className="font-mono text-sm font-semibold truncate text-purple-300">
                            {page.highlightedName}
                          </div>
                          <div className="text-xs text-gray-400 mt-1 line-clamp-2">
                            {page.highlightedTitle}
                          </div>
                        </button>

                        {/* Snippets in nested boxes */}
                        {page.snippets && page.snippets.length > 0 && (
                          <div className="px-4 pb-3 space-y-2">
                            {page.snippets.map((snippet, idx) => {
                              const snippetLines = snippet.text.split("\n");
                              const [
                                _beforeLineNum,
                                _matchLineNum,
                                _afterLineNum,
                              ] = snippet.lineNumbers;
                              return (
                                <div
                                  key={idx}
                                  data-snippet-id={`${page.name}-${idx}`}
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
                    {moreFilesCount > 0 && (
                      <div className="bg-fuchsia-900/50 border border-fuchsia-700/50 rounded px-3 py-2 text-sm font-semibold text-fuchsia-200 m-3">
                        ... {moreFilesCount} more matching file
                        {moreFilesCount !== 1 ? "s" : ""}
                      </div>
                    )}
                  </div>
                )}
              </div>
            </div>

            {/* Content viewer */}
            <div className="flex-1 min-w-0">
              {pageNotFound ? (
                <div className="bg-gray-900/30 border border-gray-800 rounded-lg p-6 overflow-y-auto h-[calc(100vh-300px)] flex flex-col items-center justify-center">
                  <div className="text-center">
                    <h2 className="text-7xl font-bold text-red-400 mb-6">
                      404
                    </h2>
                    <p className="text-3xl text-red-400 font-semibold mb-8">
                      Page Not Found
                    </p>
                    <div className="text-gray-400 text-lg">
                      <p className="mb-6">
                        The documentation page for{" "}
                        <code className="bg-gray-800 px-3 py-2 rounded text-xl">
                          {selectedPageName}
                        </code>{" "}
                        could not be found.
                      </p>
                      <p className="text-gray-500">
                        Try searching for a different page or check the
                        spelling.
                      </p>
                    </div>
                  </div>
                </div>
              ) : selectedPageContent ? (
                <div
                  ref={contentViewerRef}
                  className="bg-gray-900/30 border border-gray-800 rounded-lg p-6 overflow-y-auto max-h-[calc(100vh-300px)]"
                >
                  <div className="mb-4 pb-4 border-b border-gray-800">
                    <h2 className="text-2xl font-bold text-purple-400">
                      {selectedPageName}(3)
                    </h2>
                  </div>
                  <div className="man-page-content">
                    {renderContentWithCodeBlocks(
                      selectedPageContent,
                      selectedPageName?.endsWith("_source") || selectedPageName?.endsWith(".c") || false,
                      searchQuery,
                      targetLineNumber,
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
          <div className="mt-8">
            <Footer />
          </div>
        </div>
      </div>
    </>
  );
}
