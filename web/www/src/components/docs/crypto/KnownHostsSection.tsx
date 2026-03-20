import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";
import TrackedLink from "../../TrackedLink";

export default function KnownHostsSection() {
  return (
    <section className="mb-16">
      <Heading
        level={2}
        className="text-3xl font-bold text-teal-400 mb-6 border-b border-teal-900/50 pb-2"
      >
        📋 Known Hosts (TOFU)
      </Heading>

      <p className="text-gray-300 mb-6">
        ascii-chat tracks server identities with an SSH-style{" "}
        <strong className="text-teal-300">Trust-On-First-Use (TOFU)</strong>{" "}
        model. The first time you connect to a server, its public key is saved.
        Subsequent connections verify the key hasn't changed.
      </p>

      <div className="space-y-6">
        <div className="bg-gray-900/50 border border-cyan-900/30 rounded-lg p-6">
          <Heading level={3} className="text-cyan-300 font-semibold mb-3">
            File Location
          </Heading>
          <p className="text-gray-300 mb-2">Known server keys are stored in:</p>
          <div className="space-y-2">
            <div>
              <p className="text-gray-400 text-sm mb-1">Unix/Linux/macOS:</p>
              <CodeBlock language="text">{``}</CodeBlock>
              <p className="text-gray-400 text-xs mt-1">
                (or{" "}
                <code className="text-purple-400 bg-gray-950 px-1 py-0.5 rounded">
                  $XDG_CONFIG_HOME/ascii-chat/known_hosts
                </code>{" "}
                if XDG_CONFIG_HOME is set)
              </p>
            </div>
            <div>
              <p className="text-gray-400 text-sm mb-1">Windows:</p>
              <CodeBlock language="text">{``}</CodeBlock>
            </div>
          </div>
          <p className="text-gray-300 text-sm mt-3">
            This file is created automatically on first connection. It's
            readable and editable as plain text.
          </p>
        </div>

        <div className="bg-gray-900/50 border border-purple-900/30 rounded-lg p-6">
          <Heading level={3} className="text-purple-300 font-semibold mb-3">
            File Format
          </Heading>
          <p className="text-gray-300 mb-3">
            Each line represents one known server:
          </p>
          <CodeBlock language="text">{`<IP:port> x25519 <hex_public_key> [optional comment]`}</CodeBlock>
          <p className="text-gray-300 mb-3 mt-3">Example entries:</p>
          <CodeBlock language="text">{`192.168.1.100:27015 x25519 a1b2c3d4... server-laptop
10.0.0.5:27015 x25519 5e6f7890... production-server
[2001:db8::1]:27015 x25519 12345678... ipv6-server`}</CodeBlock>
          <p className="text-gray-300 text-sm mt-3">
            <strong className="text-purple-400">Note:</strong> IPv6 addresses
            use bracket notation{" "}
            <code className="text-pink-400 bg-gray-950 px-2 py-1 rounded">
              [address]:port
            </code>{" "}
            to distinguish the colons in the address from the port separator.
          </p>
        </div>

        <div className="bg-gray-900/50 border border-teal-900/30 rounded-lg p-6">
          <Heading level={3} className="text-teal-300 font-semibold mb-3">
            How It Works
          </Heading>
          <ol className="list-decimal list-inside space-y-2 text-gray-300">
            <li>
              <strong className="text-cyan-400">First Connection:</strong>{" "}
              Server's key is saved to known_hosts automatically. Connection
              proceeds.
            </li>
            <li>
              <strong className="text-purple-400">
                Subsequent Connections:
              </strong>{" "}
              Client checks if the server's key matches the saved key.
            </li>
            <li>
              <strong className="text-teal-400">Key Matches:</strong> Connection
              proceeds silently.
            </li>
            <li>
              <strong className="text-pink-400">Key Mismatch:</strong>{" "}
              Connection is rejected with a MITM warning. User must take action.
            </li>
          </ol>
        </div>

        <div className="bg-yellow-900/20 border border-yellow-700/50 rounded-lg p-6">
          <Heading level={3} className="text-yellow-300 font-semibold mb-3">
            ⚠️ When Keys Change (MITM Warning)
          </Heading>
          <p className="text-gray-300 mb-3">
            If a server's key changes, you'll see a warning like:
          </p>
          <CodeBlock language="text">{`@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!    @
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY!
Someone could be eavesdropping on you right now (man-in-the-middle attack)!

The server's key has changed:
  Expected: a1b2c3d4...
  Received: 9f8e7d6c...

Connection refused. Remove the old key from known_hosts to continue.`}</CodeBlock>
        </div>

        <div className="bg-gray-900/50 border border-pink-900/30 rounded-lg p-6">
          <Heading level={3} className="text-pink-300 font-semibold mb-3">
            🔧 What to Do When Keys Change
          </Heading>
          <p className="text-gray-300 mb-4">
            Key changes happen for legitimate reasons (server reinstalled, key
            rotated) and malicious reasons (MITM attack). Verify before
            proceeding.
          </p>

          <div className="space-y-4">
            <div>
              <Heading level={4} className="text-cyan-400 font-semibold mb-2">
                Step 1: Verify the Key Change is Legitimate
              </Heading>
              <ul className="list-disc list-inside space-y-1 text-gray-300 text-sm ml-4">
                <li>
                  Contact the server administrator through a different channel
                  (phone, Signal, etc.)
                </li>
                <li>
                  Ask them to confirm they changed keys or reinstalled the
                  server
                </li>
                <li>
                  Verify the new key fingerprint matches what they provide
                </li>
              </ul>
            </div>

            <div>
              <Heading level={4} className="text-purple-400 font-semibold mb-2">
                Step 2: Remove the Old Entry
              </Heading>
              <p className="text-gray-300 mb-2 text-sm">
                Open{" "}
                <code className="text-teal-400 bg-gray-950 px-2 py-1 rounded">
                  ~/.config/ascii-chat/known_hosts
                </code>{" "}
                in a text editor and delete the line for that IP:port
                combination:
              </p>
              <CodeBlock language="text">{`# Before (remove this line):
192.168.1.100:27015 x25519 a1b2c3d4... old-key

# After (line deleted):`}</CodeBlock>
              <p className="text-gray-300 text-sm mt-2">
                Alternatively, delete the entire file to remove all known hosts:{" "}
                <code className="text-pink-400 bg-gray-950 px-2 py-1 rounded">
                  rm ~/.config/ascii-chat/known_hosts
                </code>
              </p>
            </div>

            <div>
              <Heading level={4} className="text-teal-400 font-semibold mb-2">
                Step 3: Reconnect
              </Heading>
              <p className="text-gray-300 text-sm">
                Connect again. The new key will be saved automatically (TOFU
                model).
              </p>
            </div>
          </div>
        </div>

        <div className="bg-red-900/20 border border-red-700/50 rounded-lg p-6">
          <Heading level={3} className="text-red-300 font-semibold mb-3">
            🚨 Disabling known hosts (don't do this)
          </Heading>
          <p className="text-gray-300 mb-3">
            Set{" "}
            <code className="text-red-400 bg-gray-950 px-2 py-1 rounded">
              ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1
            </code>{" "}
            to skip known hosts checks.
          </p>
          <p className="text-red-300 font-semibold mb-2">
            ⚠️ This turns off MITM protection. Anyone can intercept your
            connection.
          </p>
          <p className="text-gray-300 text-sm">
            Only use this for local testing. See the{" "}
            <TrackedLink
              to="/man1#ENVIRONMENT"
              label="Crypto - Env Vars Insecure"
              className="text-green-400 hover:text-green-300 underline"
            >
              Man Page ENVIRONMENT section
            </TrackedLink>{" "}
            for details.
          </p>
        </div>
      </div>
    </section>
  );
}
