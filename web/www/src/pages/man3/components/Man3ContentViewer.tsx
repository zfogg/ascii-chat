import { renderContentWithCodeBlocks } from "../utils";
import type { RefObject } from "react";

interface Man3ContentViewerProps {
  contentViewerRef: RefObject<HTMLDivElement | null>;
  selectedPageName: string | null;
  selectedPageContent: string | null;
  searchQuery: string;
  targetLineNumber: number | null;
}

/**
 * Displays the content of a selected man page with code block rendering
 */
export function Man3ContentViewer({
  contentViewerRef,
  selectedPageName,
  selectedPageContent,
  searchQuery,
  targetLineNumber,
}: Man3ContentViewerProps) {
  const isSourcePage =
    selectedPageName?.endsWith("_source") || selectedPageName?.endsWith(".c");

  return (
    <div
      ref={contentViewerRef}
      className="h-full w-full bg-gray-899/30 border border-gray-800 rounded-lg p-6 overflow-y-auto"
    >
      <div className="mb-4 pb-4 border-b border-gray-800">
        <h2 className="text-2xl font-bold text-purple-400">
          {selectedPageName}(3)
        </h2>
      </div>
      <div className="man-page-content">
        {renderContentWithCodeBlocks(
          selectedPageContent ?? "",
          isSourcePage,
          searchQuery,
          targetLineNumber,
        )}
      </div>
    </div>
  );
}
