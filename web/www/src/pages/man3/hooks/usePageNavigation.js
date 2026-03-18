import { useCallback, useEffect, useRef, useState } from "react";
import { setBreadcrumbSchema } from "../../../utils/breadcrumbs";

/**
 * Hook for managing page navigation, loading, history, and scrolling
 *
 * @param {Array} manPages - Full page index
 * @param {Function} processPageContent - HTML preprocessing function (includes GitHub link processing)
 * @param {string} searchQuery - Current search query (optional, defaults to empty string)
 * @param {string} providedCommitSha - Git commit SHA for GitHub links
 * @returns {{ selectedPageContent, selectedPageName, pageNotFound, targetLineNumber, targetSnippetIndex, targetSnippetText, contentViewerRef, loadPageContent, and state setters }}
 */
export function usePageNavigation(
  manPages,
  processPageContent,
  searchQuery = "",
  providedCommitSha = null,
) {
  const [selectedPageContent, setSelectedPageContent] = useState(null);
  const [selectedPageName, setSelectedPageName] = useState(null);
  const [pageNotFound, setPageNotFound] = useState(false);
  const [targetLineNumber, setTargetLineNumber] = useState(null);
  const [targetSnippetIndex, setTargetSnippetIndex] = useState(null);
  const [targetSnippetText, setTargetSnippetText] = useState(null);
  const contentViewerRef = useRef(null);
  const commitSha = providedCommitSha || "master";

  console.log("[usePageNavigation] commitSha:", commitSha);

  // Load page content from file and update state
  const loadPageContent = useCallback(
    (
      pageName,
      lineNumber = null,
      snippetIndex = null,
      skipHistoryPush = false,
      snippetText = null,
    ) => {
      if (!pageName) return;

      // Find the page in manPages to get the correct filename
      const page = manPages.find((p) => p.name === pageName);
      const filename = page?.file || `ascii-chat-${pageName}.html`;

      fetch(`/man3/${filename}`)
        .then((r) => {
          if (!r.ok) {
            setPageNotFound(true);
            setSelectedPageContent(null);
            setSelectedPageName(pageName);
            return null;
          }
          return r.text();
        })
        .then((html) => {
          if (!html) return;

          setPageNotFound(false);

          // Clear scroll to top on new page, UNLESS there's a hash target
          // (don't scroll to top if we need to scroll to an anchor instead)
          if (contentViewerRef.current && !window.location.hash) {
            contentViewerRef.current.scrollTop = 0;
          }

          // Read current search query from URL to ensure we use latest value
          const currentParams = new URLSearchParams(window.location.search);
          const currentSearchQuery = currentParams.get("q") || searchQuery;

          // Apply HTML transformations in sequence
          let processedContent = processPageContent(html, currentSearchQuery);

          // Add GitHub links to "Definition at line X" references
          const page = manPages.find((p) => p.name === pageName);
          const sourcePath = page?.sourcePath;
          const isSourcePage = pageName?.endsWith("_source") || false;
          processedContent = processDefinitionLinks(
            processedContent,
            sourcePath,
            commitSha,
            isSourcePage,
          );

          setSelectedPageContent(processedContent);
          setSelectedPageName(pageName);
          if (lineNumber) {
            setTargetLineNumber(lineNumber);
            setTargetSnippetIndex(snippetIndex);
            setTargetSnippetText(snippetText);
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
    [processPageContent, searchQuery, manPages, commitSha],
  );

  // Initialize breadcrumbs and load initial page from URL
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "API Reference", path: "/man3" },
    ]);

    // Load from URL params
    const params = new URLSearchParams(window.location.search);
    const pageParam = params.get("page");

    if (pageParam) {
      const pageName = decodeURIComponent(pageParam);
      // Trigger load without history push since we're hydrating from URL
      loadPageContent(pageName, null, null, true);
    }
  }, [loadPageContent]); // Depend on loadPageContent

  // Left-panel snippet scroll: scroll snippet into view in left panel
  useEffect(() => {
    if (
      targetSnippetIndex !== null &&
      selectedPageName &&
      typeof document !== "undefined"
    ) {
      // Find the snippet element in the left panel and scroll it into view
      setTimeout(() => {
        // Create unique ID: page-name + snippet-index
        const snippetId = `${selectedPageName}-${targetSnippetIndex}`;
        const targetElement = document.querySelector(
          `[data-snippet-id="${snippetId}"]`,
        );

        if (targetElement) {
          // Scroll the snippet into view with smooth behavior
          targetElement.scrollIntoView({ behavior: "smooth", block: "center" });
        }
      }, 100);
    }
  }, [targetSnippetIndex, selectedPageName]);

  // Right-panel scroll: scroll to target line or snippet text
  useEffect(() => {
    if (!selectedPageContent || !contentViewerRef.current) return;

    const viewer = contentViewerRef.current;

    // Delay to allow content to render
    setTimeout(() => {
      // Clear any previous yellow highlighting
      const allHighlighted = viewer.querySelectorAll(
        '[style*="background-color"]',
      );
      for (const el of allHighlighted) {
        if (
          el.style.backgroundColor === "rgb(255, 191, 36)" ||
          el.style.backgroundColor === "#fbbf24"
        ) {
          el.style.backgroundColor = "";
        }
      }

      // First, try to find and scroll to snippet text in non-codeblock content
      if (targetSnippetText && !targetLineNumber) {
        const walker = document.createTreeWalker(
          viewer,
          NodeFilter.SHOW_TEXT,
          null,
          false,
        );

        let foundNode = null;
        let node;

        // Find first text node containing significant portion of the snippet
        // Extract just the matched text (first line of snippet usually has the match)
        const firstLine = targetSnippetText.split("\n")[0];
        const searchTerm = firstLine.trim();

        while ((node = walker.nextNode())) {
          if (node.textContent.includes(searchTerm)) {
            foundNode = node;
            break;
          }
        }

        if (foundNode) {
          let scrollTarget = foundNode.parentElement;

          // Walk up to find a reasonable scrollable parent (at least a paragraph or section)
          while (
            scrollTarget &&
            scrollTarget !== viewer &&
            scrollTarget.offsetHeight === 0
          ) {
            scrollTarget = scrollTarget.parentElement;
          }

          // Walk up to find a meaningful container (like a section or paragraph)
          let containerWalks = 0;
          while (
            scrollTarget &&
            scrollTarget !== viewer &&
            containerWalks < 5 &&
            scrollTarget.tagName === "SPAN"
          ) {
            scrollTarget = scrollTarget.parentElement;
            containerWalks++;
          }

          if (scrollTarget && scrollTarget !== viewer) {
            scrollTarget.scrollIntoView({ behavior: "smooth", block: "start" });
            setTargetSnippetText(null); // Clear after scrolling
            return;
          }
        }
      }

      // Scroll to target line if set (when clicking search snippets)
      if (targetLineNumber) {
        // Search for any element that contains the target line number
        const walker = document.createTreeWalker(
          viewer,
          NodeFilter.SHOW_TEXT,
          null,
          false,
        );

        let foundElement = null;
        let node;

        // Collect all potential matches and prioritize exact line number matches
        const potentialMatches = [];

        // Walk through all text nodes looking for the line number
        while ((node = walker.nextNode())) {
          const text = node.textContent || "";
          const trimmed = text.trim();

          // Priority 1: Exact match - text is ONLY the line number (with possible whitespace)
          if (trimmed === String(targetLineNumber)) {
            potentialMatches.push({ node, priority: 1, text }); // Push to preserve order
          } // Priority 2: Line number followed by whitespace only
          else if (new RegExp(`^${targetLineNumber}\\s*$`).test(trimmed)) {
            potentialMatches.push({ node, priority: 2, text });
          } // Priority 3: Line number in mixed content (fallback)
          else if (
            new RegExp(`(^|\\D)${targetLineNumber}(?:\\D|$)`).test(text)
          ) {
            potentialMatches.push({ node, priority: 3, text });
          }
        }

        // Use the best match found
        if (potentialMatches.length > 0) {
          // Sort by priority (lower is better, preserves document order for same priority)
          potentialMatches.sort((a, b) => a.priority - b.priority);
          node = potentialMatches[0].node;
          foundElement = node.parentElement;

          // Walk up to find a reasonable scrollable/visible parent
          let parent = foundElement;
          while (parent && parent !== viewer && parent.offsetHeight === 0) {
            parent = parent.parentElement;
          }
          if (parent && parent !== viewer) {
            foundElement = parent;
          }
        }

        if (foundElement) {
          // Get all spans in the CODE block and find those on the same line
          const codeBlock =
            foundElement.closest("code") || foundElement.closest("pre");

          if (codeBlock) {
            // Find the offsetTop of our element to identify its line
            const targetTop = foundElement.offsetTop;

            // Find all spans/elements in the code block
            const allSpans = codeBlock.querySelectorAll("span, div, *");

            // Collect all spans on this line that have non-whitespace content
            const lineSpans = [];
            for (const span of allSpans) {
              if (
                span.offsetTop === targetTop &&
                span.offsetHeight > 0 &&
                span.offsetHeight < 100
              ) {
                const txt = span.textContent || "";
                if (txt.trim().length > 0) {
                  lineSpans.push(span);
                }
              }
            }

            // Sort by offsetLeft to process left-to-right, then by DOM order
            lineSpans.sort((a, b) => {
              if (Math.abs(a.offsetLeft - b.offsetLeft) > 1) {
                return a.offsetLeft - b.offsetLeft;
              }
              return 0;
            });

            if (lineSpans.length > 0) {
              // Highlight the ENTIRE LINE by applying background to all spans on it
              for (const el of lineSpans) {
                el.style.backgroundColor = "#fbbf24";
                el.style.color = "#000000";
                if (el.offsetHeight === 0) {
                  el.style.display = "inline";
                  el.style.visibility = "visible";
                  el.style.opacity = "1";
                }
              }

              lineSpans[0].scrollIntoView({
                behavior: "auto",
                block: "center",
              });
            }
          }
        } else {
          // For documentation pages without line numbers, search for the matching text
          if (searchQuery && contentViewerRef.current) {
            try {
              const searchRegex = new RegExp(searchQuery, "i");
              const tw = document.createTreeWalker(
                contentViewerRef.current,
                NodeFilter.SHOW_TEXT,
                null,
                false,
              );

              let foundN = null;
              let nd;

              // Find first text node matching the search query
              while ((nd = tw.nextNode())) {
                if (searchRegex.test(nd.textContent)) {
                  foundN = nd;
                  break;
                }
              }

              if (foundN) {
                // Scroll to the parent element of the match
                let scrollTgt = foundN.parentElement;

                // Walk up to find a reasonable scrollable parent
                while (
                  scrollTgt &&
                  scrollTgt !== viewer &&
                  scrollTgt.offsetHeight === 0
                ) {
                  scrollTgt = scrollTgt.parentElement;
                }

                if (scrollTgt && scrollTgt !== viewer) {
                  scrollTgt.scrollIntoView({
                    behavior: "smooth",
                    block: "start",
                  });
                }
              }
            } catch (e) {
              // Silent fail on regex error
            }
          }
        }
      }
    }, 100);
  }, [
    selectedPageContent,
    selectedPageName,
    targetLineNumber,
    targetSnippetText,
    searchQuery,
  ]);

  // Handle Doxygen link interception
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
        const pn = decodeURIComponent(newMatch[1]);
        let ln = null;
        if (newMatch[2]) {
          // Extract number from "l00046" format
          const numStr = newMatch[2].replace(/^l/, "").split("-")[0];
          ln = parseInt(numStr, 10);
        }
        loadPageContent(pn, ln);
        return;
      }

      // Also match old Doxygen source file links for backward compatibility: /man3/filename.html#l00123
      const doxygenMatch = href.match(/\/man3\/(.+?)\.html(#l\d+)?$/);
      if (doxygenMatch) {
        e.preventDefault();
        const pn = doxygenMatch[1];
        const lineAnchor = doxygenMatch[2] || "";

        // Load the page and scroll to line if specified
        loadPageContent(pn);

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

  // Helper function to scroll to hash target
  const scrollToHash = (hash) => {
    if (!hash || !contentViewerRef.current) return;
    const container = contentViewerRef.current;
    const elementId = hash.substring(1); // Remove the # prefix
    const targetElement = container.querySelector(`[id="${elementId}"]`);
    if (targetElement) {
      targetElement.scrollIntoView({ behavior: "auto", block: "start" });
    }
  };

  // Handle initial hash on page load and hash changes from link clicks
  useEffect(() => {
    if (!selectedPageContent || !contentViewerRef.current) return;

    const hash = window.location.hash;
    if (!hash) return;

    // Wait for DOM to be fully rendered before scrolling
    setTimeout(() => {
      scrollToHash(hash);
    }, 100);
  }, [selectedPageContent]);

  // Scroll to hash when user clicks a link on the same page (hashchange event)
  useEffect(() => {
    const handleHashChange = () => {
      const hash = window.location.hash;
      if (hash) {
        // Small delay to ensure DOM is ready
        setTimeout(() => {
          scrollToHash(hash);
        }, 100);
      }
    };

    window.addEventListener("hashchange", handleHashChange);
    return () => window.removeEventListener("hashchange", handleHashChange);
  }, []);

  // Browser back/forward (popstate)
  useEffect(() => {
    const handlePopState = () => {
      const params = new URLSearchParams(window.location.search);
      const pageParam = params.get("page");

      if (pageParam) {
        const pageName = decodeURIComponent(pageParam);
        loadPageContent(pageName, null, null, true);
      } else {
        setSelectedPageContent(null);
        setSelectedPageName(null);
      }
    };

    window.addEventListener("popstate", handlePopState);
    return () => window.removeEventListener("popstate", handlePopState);
  }, [loadPageContent]);

  // Functions table DOM transform
  useEffect(() => {
    const functionsHeading = document.getElementById("Functions");
    if (!functionsHeading) return;

    // Find the P tag with functions
    let pTag = functionsHeading.nextElementSibling;
    while (pTag && pTag.tagName !== "P") {
      pTag = pTag.nextElementSibling;
    }

    if (!pTag || !pTag.textContent.includes("(")) return;

    // Split the HTML by <br> tags
    const html = pTag.innerHTML;
    const sections = html.split(/<br\s*\/?>/i);

    const rows = [];

    // Process sections in pairs: signature + description
    for (let i = 0; i < sections.length - 1; i += 2) {
      const sig = sections[i].trim();
      const desc = i + 1 < sections.length ? sections[i + 1].trim() : "";

      if (!sig) continue;

      // Check if sig section looks like a function signature
      const tempDiv = document.createElement("div");
      tempDiv.innerHTML = sig;
      const sigText = tempDiv.textContent.trim();

      // It's a signature if it contains parentheses and likely starts with a type or contains a bold function name
      if (sigText.includes("(") && sigText.includes(")")) {
        rows.push({
          signature: sig,
          description: desc,
        });
      }
    }

    // Build table from parsed rows
    if (rows.length === 0) return;

    const tbody = document.createElement("tbody");
    for (const row of rows) {
      const tr = document.createElement("tr");
      tr.innerHTML = `<td class="man-data-field-type" style="font-family: monospace; font-size: 0.85em; word-break: break-word;">${row.signature}</td><td class="man-data-field-desc">${row.description}</td>`;
      tbody.appendChild(tr);
    }

    if (tbody.children.length > 0) {
      const table = document.createElement("table");
      table.className = "man-data-fields-table";
      table.appendChild(tbody);
      pTag.innerHTML = "";
      pTag.appendChild(table);
    }
  }, [selectedPageContent]);

  return {
    selectedPageContent,
    setSelectedPageContent,
    selectedPageName,
    setSelectedPageName,
    pageNotFound,
    setPageNotFound,
    targetLineNumber,
    setTargetLineNumber,
    targetSnippetIndex,
    setTargetSnippetIndex,
    targetSnippetText,
    setTargetSnippetText,
    contentViewerRef,
    loadPageContent,
  };
}
