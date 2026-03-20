interface Man3NotFoundProps {
  selectedPageName: string | null;
}

/**
 * 404 Not Found state for Man3 pages
 */
export function Man3NotFound({ selectedPageName }: Man3NotFoundProps) {
  return (
    <div className="bg-gray-900/30 border border-gray-800 rounded-lg p-6 overflow-y-auto flex-1 flex flex-col items-center justify-center">
      <div className="text-center">
        <h2 className="text-7xl font-bold text-red-400 mb-6">404</h2>
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
            Try searching for a different page or check the spelling.
          </p>
        </div>
      </div>
    </div>
  );
}
