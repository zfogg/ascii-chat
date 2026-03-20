import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function BroadcastingSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-blue-400">
        📡 Broadcasting to Clients
      </Heading>

      <p className="docs-paragraph">
        Run a server streaming media, and have multiple clients view
        simultaneously:
      </p>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-blue-300 mb-3">
          Server: Stream Local File
        </Heading>
        <CodeBlock language="bash">
          {
            "# Server: Mirror local file to all connected clients\nascii-chat server --file movie.mp4\n\n# Server: With custom settings\nascii-chat server --file movie.mp4 --fps 30 --width 160 --audio\n\n# Server: On specific port with password\nascii-chat server --file movie.mp4 --port 8080 --password 'watch123'\n\n# Server: Register with ACDS (memorable session string)\nascii-chat server --file movie.mp4 --acds --password 'watch123'\n\n# Server: Stream file repeatedly\nascii-chat server --file movie.mp4 --loop --acds"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          Server: Stream Remote URL
        </Heading>
        <CodeBlock language="bash">
          {
            "# Server: Stream YouTube to all clients\nascii-chat server --url 'https://youtu.be/7ynHVGCehoM' --acds\n\n# Server: Stream live HLS broadcast\nascii-chat server --url 'https://stream.example.com/live.m3u8' \\\n  --fps 15 --acds --password 'live123'\n\n# Server: Stream with high quality\nascii-chat server --url 'https://example.com/video.mp4' \\\n  --color-mode truecolor --acds"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          Client: Watch Broadcast
        </Heading>
        <CodeBlock language="bash">
          {
            "# Connect to local broadcast via IP\nascii-chat client 192.168.1.50:27224\n\n# Connect with password\nascii-chat client --password 'watch123' 192.168.1.50\n\n# Join via ACDS session string\nascii-chat --password 'watch123' happy-movie-night\n\n# Adjust for slow network\nascii-chat --fps 15 --color-mode mono happy-movie-night\n\n# Save broadcast to file\nascii-chat --snapshot happy-movie-night > recording.txt"
          }
        </CodeBlock>
      </div>
    </section>
  );
}
