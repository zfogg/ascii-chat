import { useEffect } from "react";

/**
 * Custom hook to scroll to hash/anchor on page load
 * @param {number} delay - Delay in ms before scrolling (default: 0)
 */
export function useScrollToHash(delay = 0) {
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
    }
  }, [delay]);
}
