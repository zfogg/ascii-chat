import { useEffect, ReactNode } from "react";

export interface HeadProps {
  // Core
  title: string;
  description: string;
  keywords?: string;
  author?: string;

  // URLs
  url: string;
  ogImage?: string;
  ogImageWidth?: string;
  ogImageHeight?: string;
  ogImageAlt?: string;

  // Twitter
  twitterCard?: "summary" | "summary_large_image";
  twitterSite?: string;
  twitterCreator?: string;

  // Type
  ogType?: string;

  // Additional meta tags
  children?: ReactNode;
}

export function Head({
  title,
  description,
  keywords,
  author = "Zach Fogg",
  url,
  ogImage,
  ogImageWidth = "1200",
  ogImageHeight = "630",
  ogImageAlt,
  twitterCard = "summary_large_image",
  twitterSite = "@ascii_chat",
  twitterCreator = "@zach_fogg",
  ogType = "website",
  children,
}: HeadProps) {
  useEffect(() => {
    // Update title
    document.title = title;

    // Helper to set meta tag
    const setMeta = (name: string, content: string, isProperty = false) => {
      let meta = document.querySelector(
        `meta[${isProperty ? "property" : "name"}="${name}"]`
      );
      if (!meta) {
        meta = document.createElement("meta");
        if (isProperty) {
          meta.setAttribute("property", name);
        } else {
          meta.setAttribute("name", name);
        }
        document.head.appendChild(meta);
      }
      meta.setAttribute("content", content);
    };

    // Set standard meta tags
    setMeta("description", description);
    if (keywords) setMeta("keywords", keywords);
    if (author) setMeta("author", author);

    // Set Open Graph tags
    setMeta("og:type", ogType, true);
    setMeta("og:url", url, true);
    setMeta("og:title", title, true);
    setMeta("og:description", description, true);

    if (ogImage) {
      setMeta("og:image", ogImage, true);
      setMeta("og:image:width", ogImageWidth, true);
      setMeta("og:image:height", ogImageHeight, true);
      if (ogImageAlt) {
        setMeta("og:image:alt", ogImageAlt, true);
      }
    }

    // Set Twitter tags
    setMeta("twitter:card", twitterCard, true);
    setMeta("twitter:site", twitterSite, true);
    setMeta("twitter:creator", twitterCreator, true);
    setMeta("twitter:url", url, true);
    setMeta("twitter:title", title, true);
    setMeta("twitter:description", description, true);

    if (ogImage) {
      setMeta("twitter:image", ogImage, true);
      if (ogImageAlt) {
        setMeta("twitter:image:alt", ogImageAlt, true);
      }
    }
  }, [
    title,
    description,
    keywords,
    author,
    url,
    ogImage,
    ogImageWidth,
    ogImageHeight,
    ogImageAlt,
    twitterCard,
    twitterSite,
    twitterCreator,
    ogType,
  ]);

  return <>{children}</>;
}
