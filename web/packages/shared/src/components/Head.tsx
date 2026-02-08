import { Helmet } from 'react-helmet-async';
import { ReactNode } from 'react';

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
  twitterCard?: 'summary' | 'summary_large_image';
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
  author = 'Zach Fogg',
  url,
  ogImage,
  ogImageWidth = '1200',
  ogImageHeight = '630',
  ogImageAlt,
  twitterCard = 'summary_large_image',
  twitterSite = '@ascii_chat',
  twitterCreator = '@zach_fogg',
  ogType = 'website',
  children,
}: HeadProps) {
  return (
    <Helmet>
      <title>{title}</title>
      <meta name="description" content={description} />
      {keywords && <meta name="keywords" content={keywords} />}
      {author && <meta name="author" content={author} />}

      {/* Open Graph */}
      <meta property="og:type" content={ogType} />
      <meta property="og:url" content={url} />
      <meta property="og:title" content={title} />
      <meta property="og:description" content={description} />
      {ogImage && (
        <>
          <meta property="og:image" content={ogImage} />
          <meta property="og:image:width" content={ogImageWidth} />
          <meta property="og:image:height" content={ogImageHeight} />
          {ogImageAlt && <meta property="og:image:alt" content={ogImageAlt} />}
        </>
      )}

      {/* Twitter */}
      <meta property="twitter:card" content={twitterCard} />
      <meta property="twitter:site" content={twitterSite} />
      <meta property="twitter:creator" content={twitterCreator} />
      <meta property="twitter:url" content={url} />
      <meta property="twitter:title" content={title} />
      <meta property="twitter:description" content={description} />
      {ogImage && (
        <>
          <meta property="twitter:image" content={ogImage} />
          {ogImageAlt && <meta property="twitter:image:alt" content={ogImageAlt} />}
        </>
      )}

      {/* Custom children for route-specific tags */}
      {children}
    </Helmet>
  );
}
