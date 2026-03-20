interface Man3HeaderProps {
  searchQuery: string;
  setSearchQuery: (query: string) => void;
  regexError: string | null;
  searching: boolean;
  filesMatched: number;
  totalMatches: number;
}

/**
 * Header section with title, search input, and status
 */
export function Man3Header({
  searchQuery,
  setSearchQuery,
  regexError,
  searching,
  filesMatched,
  totalMatches,
}: Man3HeaderProps) {
  return (
    <header className="flex-shrink-0 px-4 sm:px-6 pb-4 pt-8 sm:pt-12 max-w-4xl mx-auto w-full">
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
          <div className="flex flex-col lg:flex-row items-center lg:justify-between gap-4 mt-2">
            <p className="text-xs text-gray-500 text-center lg:text-left">
              Regex search (default case-insensitive). Examples:{" "}
              <code
                onClick={() => setSearchQuery("socket_t")}
                className="bg-gray-800 px-1 rounded cursor-pointer underline hover:bg-gray-700 hover:text-gray-100 transition-colors"
              >
                socket_t
              </code>
              ,{" "}
              <code
                onClick={() => setSearchQuery("error|crypto")}
                className="bg-gray-800 px-1 rounded cursor-pointer underline hover:bg-gray-700 hover:text-gray-100 transition-colors"
              >
                error|crypto
              </code>
              , or{" "}
              <code
                onClick={() => setSearchQuery("/^socket$/gi")}
                className="bg-gray-800 px-1 rounded cursor-pointer underline hover:bg-gray-700 hover:text-gray-100 transition-colors"
              >
                /^socket$/gi
              </code>{" "}
              for flags
            </p>
            <p className="text-xs text-gray-500 text-center text-right self-end">
              📖{" "}
              <a
                href="https://zfogg.github.io/ascii-chat/"
                target="_blank"
                rel="noopener noreferrer"
                className="text-cyan-400 hover:text-cyan-300 transition-colors"
              >
                Doxygen HTML Documentation
              </a>
            </p>
          </div>
        )}
      </div>
      <p className="text-sm text-gray-400 mt-3 text-center">
        {searching
          ? "Searching..."
          : filesMatched > 0
            ? `${filesMatched} file${
                filesMatched !== 1 ? "s" : ""
              } matched, ${totalMatches} match${totalMatches !== 1 ? "es" : ""}`
            : "No results"}
      </p>
    </header>
  );
}
