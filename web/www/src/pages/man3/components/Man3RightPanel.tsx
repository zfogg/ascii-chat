import { Man3NotFound } from "./Man3NotFound";
import { Man3ContentViewer } from "./Man3ContentViewer";
import { Man3EmptyState } from "./Man3EmptyState";

/**
 * Right panel dispatcher - shows NotFound, ContentViewer, or EmptyState
 */
export function Man3RightPanel({
  pageNotFound,
  selectedPageContent,
  selectedPageName,
  contentViewerRef,
  searchQuery,
  targetLineNumber,
  searchResults,
}) {
  return (
    <div className="flex-1 flex min-w-0">
      {pageNotFound ? (
        <Man3NotFound selectedPageName={selectedPageName} />
      ) : selectedPageContent ? (
        <Man3ContentViewer
          contentViewerRef={contentViewerRef}
          selectedPageName={selectedPageName}
          selectedPageContent={selectedPageContent}
          searchQuery={searchQuery}
          targetLineNumber={targetLineNumber}
        />
      ) : (
        <Man3EmptyState searchResults={searchResults} />
      )}
    </div>
  );
}
