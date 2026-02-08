import { Head, HeadProps } from '@ascii-chat/shared/components';

type ACDSHeadProps = Partial<HeadProps> & {
  title?: string;
  description?: string;
  url?: string;
};

export function ACDSHead({
  title = 'ACDS - ascii-chat Discovery Service',
  description = 'Official ACDS (ascii-chat Discovery Service) public keys and documentation. Enables session discovery using memorable three-word strings for terminal-based video chat.',
  keywords = 'ascii-chat, ACDS, discovery service, session discovery, terminal video chat, WebRTC, NAT traversal',
  url = 'https://discovery.ascii-chat.com/',
  ogImage = 'https://discovery.ascii-chat.com/og-image.png',
  ogImageAlt = 'ACDS ascii-chat Discovery Service',
  ...props
}: ACDSHeadProps) {
  return (
    <Head
      title={title}
      description={description}
      keywords={keywords}
      url={url}
      ogImage={ogImage}
      ogImageAlt={ogImageAlt}
      {...props}
    />
  );
}
