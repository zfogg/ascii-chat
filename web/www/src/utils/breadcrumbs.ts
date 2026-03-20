interface Breadcrumb {
  name: string;
  path: string;
}

/**
 * Add breadcrumb schema to page head
 */
export function setBreadcrumbSchema(breadcrumbs: Breadcrumb[]): void {
  // Remove any existing breadcrumb script
  const existing = document.querySelector("script[data-breadcrumb]");
  if (existing) {
    existing.remove();
  }

  const breadcrumbList = {
    "@context": "https://schema.org",
    "@type": "BreadcrumbList",
    itemListElement: breadcrumbs.map((item: Breadcrumb, index: number) => ({
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
