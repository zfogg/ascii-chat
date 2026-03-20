import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function SelfHostingSection() {
  return (
    <section className="mb-12">
      <Heading
        level={2}
        className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl"
      >
        🏗️ Running Your Own ACDS Server
      </Heading>
      <p className="leading-relaxed mb-4 text-base md:text-lg">
        You can run a private ACDS server for your organization. Third-party
        ACDS servers require clients to explicitly configure your public key via
        the{" "}
        <code className="bg-gray-800 px-1 rounded">
          --discovery-service-key
        </code>{" "}
        flag.
      </p>
      <CodeBlock language="bash">
        {`# Start your own ACDS server with SSH and GPG keys
ascii-chat discovery-service 0.0.0.0 :: --port 27225 \\
  --key ~/.ssh/id_ed25519 \\
  --key gpg:YOUR_GPG_KEY_ID

# Server with GPG key
ascii-chat server --key gpg:SERVER_GPG_KEY_ID

# Client connects with explicit ACDS trust and authenticates with SSH key
ascii-chat session-name \\
  --discovery-service your-acds.example.com \\
  --discovery-service-key https://your-acds.example.com/key.pub \\
  --key ~/.ssh/id_ed25519 \\
  --server-key gpg:SERVER_GPG_KEY_ID`}
      </CodeBlock>
      <br />
      <p className="leading-relaxed mb-4 text-base md:text-lg">
        <strong>Important:</strong> You should share the public key with
        ascii-chatters in a safe way. We recommend pre-sharing them safely
        somehow or hosting them on a website at a domain you control and serving
        them over HTTPS like we do.
      </p>
    </section>
  );
}
