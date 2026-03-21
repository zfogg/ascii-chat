import { useEffect } from "react";
import { useSafeLocation } from "./useSafeLocation";

export function useAnchorNavigation(contentLoaded: boolean) {
  const location = useSafeLocation();

  useEffect(() => {
    const hash = location.hash.slice(1); // Remove the '#'
    if (!hash) return;
    // Use a small delay to ensure the DOM is fully rendered
    const timer = setTimeout(() => {
      const element = document.getElementById(hash);
      if (element) {
        element.scrollIntoView({ behavior: "smooth" });
      }
    }, 0);
    return () => clearTimeout(timer);
  }, [location.hash, contentLoaded]);
}
