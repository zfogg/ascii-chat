import { useEffect } from "react";

/**
 * React hook to handle scroll-to-hash on page load
 * Scrolls to element with matching hash in the URL
 */
export function useScrollToHash(delay = 100): void {
  useEffect(() => {
    // Only run if there's a hash
    if (!window.location.hash) return;

    const scrollToElement = () => {
      const id = window.location.hash.substring(1); // Remove #
      const element = document.getElementById(id);

      if (element) {
        element.scrollIntoView({ behavior: "smooth", block: "start" });
        // Optional: briefly highlight the element
        const originalBg = element.style.backgroundColor;
        element.style.backgroundColor = "rgba(168, 85, 247, 0.1)"; // Purple highlight
        setTimeout(() => {
          element.style.backgroundColor = originalBg;
        }, 1500);
      }
    };

    // Use timeout to allow DOM to settle
    const timer = setTimeout(scrollToElement, delay);
    return () => clearTimeout(timer);
  }, [delay]);
}
