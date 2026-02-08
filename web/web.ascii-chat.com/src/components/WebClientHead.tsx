import { Head, HeadProps } from '@ascii-chat/shared/components';

type WebClientHeadProps = Partial<HeadProps> & {
  title?: string;
  description?: string;
  url?: string;
};

export function WebClientHead({
  title = 'ascii-chat | Terminal Video Chat in Your Browser',
  description = 'Real-time video chat with ASCII art rendering, end-to-end encryption, and WebRTC support. Experience the terminal aesthetic in your browser.',
  keywords = 'ascii-chat, webassembly, wasm, browser video chat, ascii art, video conferencing, terminal, encryption, webrtc, real-time communication, mirror mode',
  url = 'https://web.ascii-chat.com/',
  ogImage = 'https://web.ascii-chat.com/og-image.png',
  ogImageAlt = 'ascii-chat web client',
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
