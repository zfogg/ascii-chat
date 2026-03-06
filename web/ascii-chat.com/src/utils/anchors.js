/**
 * Convert heading text to a URL-friendly ID
 * Examples: "Getting Started" -> "getting-started", "FAQ? Help!" -> "faq-help"
 */
export function slugify(text) {
  return text
    .toLowerCase()
    .trim()
    .replace(/[^\w\s-]/g, '') // Remove special characters
    .replace(/[\s_]+/g, '-') // Replace spaces and underscores with hyphens
    .replace(/--+/g, '-') // Replace multiple hyphens with single hyphen
    .replace(/^-+|-+$/g, ''); // Remove leading/trailing hyphens
}

/**
 * Copy anchor link to clipboard
 */
export function copyAnchorLink(id) {
  const url = `${window.location.pathname}#${id}`;
  navigator.clipboard.writeText(url).then(() => {
    // Optional: Show toast notification
    console.log(`Copied link: ${url}`);
  });
}

/**
 * React hook to handle scroll-to-hash on page load
 * Scrolls to element with matching hash in the URL
 */
export function useScrollToHash(delay = 100) {
  const { useEffect } = require('react');

  useEffect(() => {
    // Only run if there's a hash
    if (!window.location.hash) return;

    const scrollToElement = () => {
      const id = window.location.hash.substring(1); // Remove #
      const element = document.getElementById(id);

      if (element) {
        element.scrollIntoView({ behavior: 'smooth', block: 'start' });
        // Optional: briefly highlight the element
        const originalBg = element.style.backgroundColor;
        element.style.backgroundColor = 'rgba(168, 85, 247, 0.1)'; // Purple highlight
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
