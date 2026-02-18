import { Head, HeadProps } from "@ascii-chat/shared/components";

type ACDSHeadProps = Partial<HeadProps> & {
  title?: string;
  description?: string;
  url?: string;
};

export function ACDSHead({
  title = "ascii-chat Discovery Service",
  description =
    "ascii-chat Discovery Service (ACDS) public keys. ascii-chat is coded to download and trust keys from this site over HTTPS.",
  keywords =
    "ascii-chat, ascii art, discovery service, ACDS, session discovery, session signaling, terminal video chat, WebRTC, NAT traversal, peer-to-peer",
  url = "https://discovery.ascii-chat.com/",
  ogImage = "https://discovery.ascii-chat.com/og-image.png",
  ogImageAlt = "ACDS ascii-chat Discovery Service",
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
