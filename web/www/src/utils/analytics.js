export function trackLinkClick(url, label = "") {
  if (typeof window !== "undefined" && window.gtag) {
    window.gtag("event", "link_click", {
      link_url: url,
      link_label: label,
    });
  }
}
