import { useEffect, useRef, useState } from "react";

export interface ManPage {
  name: string;
  title: string;
}

/**
 * Hook for managing man page index
 * Fetches pages.json and maintains a Set of valid page names for link detection
 */
export function useManPages() {
  const [manPages, setManPages] = useState<ManPage[]>([]);
  const [loading, setLoading] = useState(true);
  const validPagesRef = useRef(new Set<string>());

  useEffect(() => {
    // Fetch the list of available man pages
    fetch("/man3/pages.json")
      .then((r) => r.json())
      .then((pages: ManPage[]) => {
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
