import { useCallback, useEffect, useRef, useState } from "react";
import { API_RELATIVE } from "@ascii-chat/shared/utils";

export interface ManPage {
  name: string;
  title: string;
  snippets?: Array<{
    lineNumber: number;
    snippet: string;
  }>;
}

interface SearchResult {
  results: ManPage[];
  filesMatched: number;
  totalMatches: number;
  moreFilesCount: number;
  error?: string;
}

/**
 * Hook for managing search functionality
 */
export function useSearch(manPages: ManPage[]) {
  const [searchQuery, setSearchQuery] = useState("");
  const [searchResults, setSearchResults] = useState<ManPage[]>([]);
  const [filesMatched, setFilesMatched] = useState(0);
  const [totalMatches, setTotalMatches] = useState(0);
  const [moreFilesCount, setMoreFilesCount] = useState(0);
  const [searching, setSearching] = useState(false);
  const [regexError, setRegexError] = useState<string | null>(null);
  const searchTimeoutRef = useRef<number | null>(null);

  // Perform search API call
  const performSearch = useCallback(
    async (query: string) => {
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
          new RegExp(regexMatch[1]!, regexMatch[2] || "i");
        } else {
          // Try as literal string with i flag
          new RegExp(query.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), "i");
        }
        setRegexError(null);
      } catch (e) {
        setRegexError((e as Error).message);
        setSearchResults([]);
        setFilesMatched(0);
        setTotalMatches(0);
        setMoreFilesCount(0);
        return;
      }

      try {
        const response = await fetch(
          `${API_RELATIVE.MAN3_SEARCH}?q=${encodeURIComponent(query)}`,
        );
        const data = (await response.json()) as SearchResult;

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
      void performSearch("");
      return;
    }

    setSearching(true);
    searchTimeoutRef.current = setTimeout(() => {
      void performSearch(searchQuery);
    }, 500);

    return () => {
      if (searchTimeoutRef.current) {
        clearTimeout(searchTimeoutRef.current);
      }
    };
  }, [searchQuery, manPages, performSearch]);

  return {
    searchQuery,
    setSearchQuery,
    searchResults,
    filesMatched,
    totalMatches,
    moreFilesCount,
    searching,
    regexError,
    performSearch,
  };
}
