declare global {
  interface Window {
    gtag?: (
      command: string,
      eventName: string,
      params?: Record<string, string>,
    ) => void;
  }
}

export function trackLinkClick(url: string, label = ""): void {
  if (typeof window !== "undefined" && window.gtag) {
    window.gtag("event", "link_click", {
      link_url: url,
      link_label: label,
    });
  }
}
