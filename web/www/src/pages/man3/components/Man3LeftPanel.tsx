import { highlightMatches } from "../utils";

/**
 * Left panel showing search results and man pages list
 */
export function Man3LeftPanel({
  loading,
  searching,
  searchQuery,
  highlightedResults,
  selectedPageName,
  loadPageContent,
  moreFilesCount,
}) {
  return (
    <div className="h-[600px] lg:h-auto lg:w-2/7 flex-shrink-0 flex flex-col">
      <h3 className="lg:hidden text-xs font-semibold text-gray-400 px-4 py-2 flex-shrink-0">
        {searchQuery ? "Results:" : "Man pages:"}
      </h3>
      <div className="h-full bg-gray-900/50 border border-gray-800 rounded-lg overflow-y-auto">
        {loading ? (
          <div className="p-4 text-center text-gray-400">Loading pages...</div>
        ) : searching && highlightedResults.length === 0 ? (
          <div className="p-4 text-center text-blue-400">Searching...</div>
        ) : highlightedResults.length === 0 ? (
          <div className="p-4 text-center text-gray-400">
            {searchQuery ? "No pages found" : "Search to get started"}
          </div>
        ) : (
          <div className="divide-y divide-gray-800">
            {highlightedResults.map((page) => (
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
                  className="w-full text-left px-4 py-3 block cursor-pointer transition-colors"
                >
                  <div className="font-mono text-sm font-semibold truncate text-purple-300 underline hover:text-purple-100">
                    {page.highlightedName}
                  </div>
                  <div className="text-xs text-gray-400 mt-1 line-clamp-2 hover:text-gray-300">
                    {page.highlightedTitle}
                  </div>
                </button>

                {/* Snippets in nested boxes */}
                {page.snippets && page.snippets.length > 0 && (
                  <div className="px-4 pb-3 space-y-2">
                    {page.snippets.map((snippet, idx) => {
                      const snippetLines = snippet.text.split("\n");
                      return (
                        <div
                          key={idx}
                          data-snippet-id={`${page.name}-${idx}`}
                          onClick={() =>
                            loadPageContent(
                              page.name,
                              snippet.lineNumbers[1],
                              idx,
                              false,
                              snippet.text,
                            )
                          }
                          className="bg-gray-950/80 border border-gray-700/50 rounded px-2 py-2 text-xs text-gray-300 font-mono overflow-x-auto cursor-pointer hover:bg-gray-900/80 hover:border-gray-600/50 transition-colors"
                        >
                          <div className="flex gap-2">
                            {/* Line numbers column */}
                            <div className="text-white text-right flex-shrink-0 select-none">
                              {snippet.lineNumbers.map((lineNum, lineIdx) => (
                                <div key={lineIdx}>{lineNum}</div>
                              ))}
                            </div>
                            {/* Code content */}
                            <div className="whitespace-nowrap overflow-x-auto flex-1">
                              {snippetLines.map((line, lineIdx) => {
                                const cleanedLine = line.replace(
                                  /^\s*\d+\s+/,
                                  "",
                                );
                                return (
                                  <div
                                    key={lineIdx}
                                    className={
                                      lineIdx ===
                                      Math.floor(snippetLines.length / 2)
                                        ? "bg-gray-800/50 px-1 -mx-1"
                                        : ""
                                    }
                                  >
                                    {highlightMatches(cleanedLine, searchQuery)}
                                  </div>
                                );
                              })}
                            </div>
                          </div>
                        </div>
                      );
                    })}
                    {page.totalMatchesInFile > page.snippets.length && (
                      <div className="bg-yellow-900/50 border border-yellow-700/50 rounded px-3 py-2 text-sm font-semibold text-yellow-200">
                        ... {page.totalMatchesInFile - page.snippets.length}{" "}
                        more matching result
                        {page.totalMatchesInFile - page.snippets.length !== 1
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
  );
}
