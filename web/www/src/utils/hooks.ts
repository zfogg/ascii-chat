import { useEffect } from "react";

/**
 * Custom hook to scroll to hash/anchor on page load
 */
export function useScrollToHash(delay = 0): void {
  useEffect(() => {
    const scrollToHash = () => {
      const hash = window.location.hash.slice(1);
      if (hash) {
        const element = document.getElementById(hash);
        if (element) {
          element.scrollIntoView({ behavior: "smooth" });
        }
      }
    };

    if (delay > 0) {
      const timer = setTimeout(scrollToHash, delay);
      return () => clearTimeout(timer);
    } else {
      scrollToHash();
      return undefined;
    }
  }, [delay]);
}
