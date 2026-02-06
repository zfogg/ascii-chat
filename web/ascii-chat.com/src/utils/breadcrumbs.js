/**
 * Add breadcrumb schema to page head
 * @param {Array} breadcrumbs - Array of {name, path} objects
 */
export function setBreadcrumbSchema(breadcrumbs) {
  // Remove any existing breadcrumb script
  const existing = document.querySelector("script[data-breadcrumb]");
  if (existing) {
    existing.remove();
  }

  const breadcrumbList = {
    "@context": "https://schema.org",
    "@type": "BreadcrumbList",
    itemListElement: breadcrumbs.map((item, index) => ({
      "@type": "ListItem",
      position: index + 1,
      name: item.name,
      item: `https://ascii-chat.com${item.path}`,
    })),
  };

  const script = document.createElement("script");
  script.type = "application/ld+json";
  script.setAttribute("data-breadcrumb", "true");
  script.textContent = JSON.stringify(breadcrumbList);
  document.head.appendChild(script);
}
