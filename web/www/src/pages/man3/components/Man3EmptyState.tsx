interface Man3EmptyStateProps {
  searchResults: unknown[];
}

/**
 * Empty state when no page is selected
 */
export function Man3EmptyState({ searchResults }: Man3EmptyStateProps) {
  return (
    <div className="h-full w-full bg-gray-900/30 border border-gray-800 rounded-lg p-12 flex items-center justify-center text-center">
      <div>
        <p className="text-gray-400 text-4xl mb-2">
          Select a page to view documentation
        </p>
        <p className="text-gray-500 text-2xl">
          {searchResults.length > 0
            ? "Click any page name in the list"
            : "Search to find API documentation"}
        </p>
      </div>
    </div>
  );
}
