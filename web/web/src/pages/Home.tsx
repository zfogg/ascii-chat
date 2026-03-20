import { Button, Heading } from "@ascii-chat/shared/components";
import { AsciiChatWebHead } from "../components";

function LinkedItemWithDescription({
  mode_name,
  description,
  color,
}: {
  mode_name: string;
  description: string;
  color: string;
}) {
  console.log(color);
  const bg_terminal_class = "bg-terminal-" + color;
  console.log(bg_terminal_class);
  return (
    <div className="flex gap-4 w-full justify-center">
      <Button
        href={`/${mode_name}`}
        className={`flex flex-col justify-center w-56 px-6 py-3 ${bg_terminal_class} text-terminal-bg rounded hover:opacity-80 transition-opacity`}
      >
        <span className="">
          {mode_name.slice(0, 1).toUpperCase()}
          {mode_name.slice(1)} Mode
        </span>
      </Button>
      <p className={`text-sm text-terminal-${color} flex-1 text-left`}>
        {description}
      </p>
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
        <p className="text-xl text-terminal-fg mb-8">
          Video chat in your <del className="opacity-60">terminal</del> browser
        </p>

        <div className="space-y-4 flex flex-col items-center max-w-4xl">
          <LinkedItemWithDescription
            mode_name="mirror"
            description="Chat with yourself! Render ASCII art of your webcam webcam, files, or URLs, without any network activity."
            color="cyan"
          />
          <LinkedItemWithDescription
            mode_name="client"
            description="Connect to ascii-chat servers via WebSocket and URL or IP over WebSocket."
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
