import { useEffect, useRef, useState } from "react";

/**
 * Hook for managing man page index
 * Fetches pages.json and maintains a Set of valid page names for link detection
 *
 * @returns {{ manPages: Array, loading: boolean, validPagesRef: React.MutableRefObject<Set> }}
 */
export function useManPages() {
  const [manPages, setManPages] = useState([]);
  const [loading, setLoading] = useState(true);
  const validPagesRef = useRef(new Set());

  useEffect(() => {
    // Fetch the list of available man pages
    fetch("/man3/pages.json")
      .then((r) => r.json())
      .then((pages) => {
        setManPages(pages);
        // Populate the valid pages set for filename link detection
        pages.forEach((page) => {
          validPagesRef.current.add(page.name);
        });
        setLoading(false);
      })
      .catch((e) => {
        console.error("Failed to load man pages index:", e);
        setLoading(false);
      });
  }, []);

  return { manPages, loading, validPagesRef };
}
