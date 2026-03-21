import { useEffect, useMemo, useRef, useState } from "react";
import { SITES } from "@ascii-chat/shared/utils";
import { Footer, AsciiChatHead } from "../../components";
import { pageMetadata } from "../../metadata";
import "../../styles/man.css";
import {
  useManPages,
  useHtmlTransforms,
  useSearch,
  usePageNavigation,
} from "./hooks";
import { Man3Header, Man3LeftPanel, Man3RightPanel } from "./components";
import { highlightMatches } from "./utils";

/**
 * Man3 - API documentation page for ascii-chat library
 * Composed of modular hooks and components
 */
export default function Man3() {
  const [sha, setSha] = useState("master");

  // Extract commit SHA from Footer's data-commit-sha attribute
  useEffect(() => {
    // Try to get from Footer's data attribute
    const footer = document.querySelector("footer[data-commit-sha]");
    if (footer) {
      const commitShaAttr = footer.getAttribute("data-commit-sha");
      if (commitShaAttr && commitShaAttr !== "__COMMIT_SHA__") {
        // Only set SHA if it's different from current to avoid infinite loops
        // eslint-disable-next-line react-hooks/set-state-in-effect
        setSha((prevSha) =>
          prevSha !== commitShaAttr ? commitShaAttr : prevSha,
        );
      }
    }
  }, []);

  // Load man page index
  const { manPages, loading, validPagesRef } = useManPages();

  // HTML transformation pipeline
  const transforms = useHtmlTransforms(validPagesRef, manPages, sha);

  // Page navigation and content loading (must be before search to maintain hook order)
  const nav = usePageNavigation(
    manPages,
    transforms.processPageContent,
    transforms.processDefinitionLinks,
    "",
    sha, // Pass the extracted/resolved SHA
  );

  // When SHA changes from initial "master" to the actual commit SHA, reload the page
  const initialShaRef = useRef("master");
  const { selectedPageName, loadPageContent } = nav;
  useEffect(() => {
    if (sha !== initialShaRef.current && selectedPageName) {
      initialShaRef.current = sha;
      loadPageContent(selectedPageName);
    }
  }, [sha, selectedPageName, loadPageContent]);

  // Search functionality
  const search = useSearch(manPages);

  // Compute highlighted results (names and titles with query matches highlighted)
  const highlightedResults = useMemo(() => {
    return search.searchResults.map((page) => ({
      name: page.name,
      title: page.title,
      file: page.name,
      highlightedName: highlightMatches(page.name, search.searchQuery),
      highlightedTitle: highlightMatches(page.title, search.searchQuery),
      ...(page.snippets
        ? {
            snippets: page.snippets.map((s) => ({
              text: s.text,
              lineNumbers: s.lineNumbers,
            })),
          }
        : {}),
    }));
  }, [search.searchResults, search.searchQuery]);

  const pageTitle = nav.selectedPageName
    ? `${nav.selectedPageName} - ascii-chat(3) | ascii-chat`
    : "ascii-chat(3) - Library Functions | ascii-chat";

  return (
    <>
      <AsciiChatHead
        title={pageTitle}
        description={pageMetadata.man3.description}
        url={`${SITES.MAIN}/man3${nav.selectedPageName ? `?page=${nav.selectedPageName}` : ""}`}
      />
      <div className="w-full h-[calc(100vh-var(--header-height))] mx-auto max-w-[2200px] xl:px-[4rem] bg-gray-950 text-gray-100 flex flex-col overflow-hidden">
        {/* Header - does not scroll */}
        <Man3Header
          searchQuery={search.searchQuery}
          setSearchQuery={search.setSearchQuery}
          regexError={search.regexError}
          searching={search.searching}
          filesMatched={search.filesMatched}
          totalMatches={search.totalMatches}
        />

        {/* Scrollable panels container */}
        <div className="flex-1 flex flex-col overflow-y-auto min-h-0">
          {/* Panels */}
          <div className="flex flex-col lg:flex-row gap-8 px-4 sm:px-6 lg:min-h-0">
            {/* Left panel - results list */}
            <Man3LeftPanel
              loading={loading}
              searching={search.searching}
              searchQuery={search.searchQuery}
              highlightedResults={highlightedResults}
              selectedPageName={nav.selectedPageName}
              loadPageContent={nav.loadPageContent}
              moreFilesCount={search.moreFilesCount}
            />

            {/* Right panel - content viewer */}
            <Man3RightPanel
              pageNotFound={nav.pageNotFound}
              selectedPageContent={nav.selectedPageContent}
              selectedPageName={nav.selectedPageName}
              contentViewerRef={nav.contentViewerRef}
              searchQuery={search.searchQuery}
              targetLineNumber={nav.targetLineNumber}
              searchResults={search.searchResults}
            />
          </div>
        </div>

        {/* Footer */}
        <div className="flex-shrink-0 p-8">
          <Footer />
        </div>
      </div>
    </>
  );
}
