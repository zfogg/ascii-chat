import { useMemo } from "react";
import Footer from "../../components/Footer";
import { AsciiChatHead } from "../../components/AsciiChatHead";
import { SITES } from "@ascii-chat/shared/utils";
import "../../styles/man.css";
import { useManPages } from "./hooks/useManPages";
import { useHtmlTransforms } from "./hooks/useHtmlTransforms";
import { useSearch } from "./hooks/useSearch";
import { usePageNavigation } from "./hooks/usePageNavigation";
import { Man3Header } from "./components/Man3Header";
import { Man3LeftPanel } from "./components/Man3LeftPanel";
import { Man3RightPanel } from "./components/Man3RightPanel";
import { highlightMatches } from "./utils/highlight.jsx";

/**
 * Man3 - API documentation page for ascii-chat library
 * Composed of modular hooks and components
 */
export default function Man3() {
  // Load man page index
  const { manPages, loading, validPagesRef } = useManPages();

  // HTML transformation pipeline
  const transforms = useHtmlTransforms(validPagesRef);

  // Page navigation and content loading
  const nav = usePageNavigation(
    manPages,
    transforms.processPageContent,
    transforms.processDefinitionLinks,
  );

  // Search functionality
  const search = useSearch(manPages);

  // Compute highlighted results (names and titles with query matches highlighted)
  const highlightedResults = useMemo(() => {
    return search.searchResults.map((page) => ({
      ...page,
      highlightedName: highlightMatches(page.name, search.searchQuery),
      highlightedTitle: highlightMatches(page.title, search.searchQuery),
    }));
  }, [search.searchResults, search.searchQuery]);

  const pageTitle = nav.selectedPageName
    ? `${nav.selectedPageName} - ascii-chat(3) | ascii-chat`
    : "ascii-chat(3) - Library Functions | ascii-chat";

  return (
    <>
      <AsciiChatHead
        title={pageTitle}
        description="C library function reference for ascii-chat. Complete API documentation with function signatures, data structures, and type definitions."
        url={`${SITES.MAIN}/man3${
          nav.selectedPageName ? `?page=${nav.selectedPageName}` : ""
        }`}
      />
      <div className="w-full h-[calc(100vh-65px)] mx-auto max-w-[2200px] xl:px-[4rem] bg-gray-950 text-gray-100 flex flex-col overflow-hidden">
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
