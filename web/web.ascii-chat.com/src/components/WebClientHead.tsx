import { Head, HeadProps } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";

type WebClientHeadProps = Partial<HeadProps> & {
  title?: string;
  description?: string;
  url?: string;
};

export function WebClientHead({
  title = "ascii-chat Web Client",
  description = "Real-time video chat with ASCII art rendering, end-to-end encryption, and WebRTC support. Experience the terminal aesthetic in your browser.",
  keywords = "ascii-chat, webassembly, wasm, browser video chat, ascii art, video conferencing, terminal, encryption, webrtc, real-time communication, mirror mode",
  url = `${SITES.WEB}/`,
  ogImage = `${SITES.WEB}/og-image.png`,
  ogImageAlt = "ascii-chat web client",
  ...props
}: WebClientHeadProps) {
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
