import { Head, HeadProps } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";

type AsciiChatHeadProps = Partial<HeadProps> & {
  title?: string;
  description?: string;
  url?: string;
};

export function AsciiChatHead({
  title = "ascii-chat - Video chat in your terminal",
  description = "Real-time terminal-based video conferencing with ASCII art rendering, end-to-end encryption, and audio support. Video chat directly in your terminal with zero-config networking.",
  keywords = "ascii-chat, terminal video chat, ascii art, video conferencing, terminal, command line, encryption, webrtc, end-to-end encryption, real-time communication",
  url = `${SITES.MAIN}/`,
  ogImage = `${SITES.MAIN}/og-image.png`,
  ogImageAlt = "ascii-chat terminal video chat application",
  twitterCreator = "@zfogg_",
  ...props
}: AsciiChatHeadProps) {
  return (
    <Head
      title={title}
      description={description}
      keywords={keywords}
      url={url}
      ogImage={ogImage}
      ogImageAlt={ogImageAlt}
      twitterCreator={twitterCreator}
      {...props}
    />
  );
}
