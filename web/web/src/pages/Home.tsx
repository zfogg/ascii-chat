import { Button } from "@ascii-chat/shared/components/Button";
import { Heading } from "@ascii-chat/shared/components/Heading";
import { AsciiChatWebHead } from "../components";

const MODE_COLORS = {
  cyan: {
    background: "bg-terminal-cyan",
    text: "text-terminal-cyan",
  },
  green: {
    background: "bg-terminal-green",
    text: "text-terminal-green",
  },
  magenta: {
    background: "bg-terminal-magenta",
    text: "text-terminal-magenta",
  },
} as const;

function LinkedItemWithDescription({
  mode_name,
  description,
  color,
}: {
  mode_name: string;
  description: string;
  color: keyof typeof MODE_COLORS;
}) {
  const colors = MODE_COLORS[color];
  return (
    <div className="flex flex-col sm:flex-row gap-4 w-full justify-center items-center">
      <Button
        href={`/${mode_name}`}
        className={`flex flex-col justify-center w-56 px-6 py-3 ${colors.background} text-terminal-bg rounded hover:opacity-80 transition-opacity order-last sm:order-first`}
      >
        <span className="">
          {mode_name.slice(0, 1).toUpperCase()}
          {mode_name.slice(1)} Mode
        </span>
      </Button>
      <p className={`text-sm ${colors.text} flex-1 text-left`}>{description}</p>
    </div>
  );
}

export function HomePage() {
  return (
    <div className="flex-1 flex items-center justify-center pt-8">
      <AsciiChatWebHead title='ascii-chat Web Client - Video chat in your browser* (* originally "terminal")' />

      <div className="text-center max-w-2xl px-8">
        <Heading
          level={1}
          className="text-4xl sm:text-6xl font-bold text-terminal-cyan mb-2"
        >
          ascii-chat
        </Heading>
        <p className="text-sm md:text-base text-terminal-8 font-normal mb-6">
          (Web Client)
        </p>
        <p className="text-xl text-terminal-fg mb-16 sm:mb-8">
          Video chat in your <del className="opacity-60">terminal</del> browser
        </p>

        <div className="space-y-10 sm:space-y-4 flex flex-col items-center max-w-4xl">
          <LinkedItemWithDescription
            mode_name="mirror"
            description="Chat with yourself! Render ASCII art of your webcam webcam, files, or URLs, without any network activity."
            color="cyan"
          />
          <LinkedItemWithDescription
            mode_name="client"
            description="Connect to ascii-chat servers via URL or IP over WebSocket."
            color="green"
          />
          <LinkedItemWithDescription
            mode_name="discovery"
            description="Create or join a linkable session where someone can see your ASCII art face over WebSocket."
            color="magenta"
          />
        </div>
      </div>
    </div>
  );
}
