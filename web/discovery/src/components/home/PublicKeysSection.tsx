import { Button, Heading, Link, PreCode } from "@ascii-chat/shared/components";

export default function PublicKeysSection({
  sshKey,
  gpgKey,
  sshFingerprint,
  gpgFingerprint,
  baseUrl,
}: {
  sshKey: string;
  gpgKey: string;
  sshFingerprint: string;
  gpgFingerprint: string;
  baseUrl: string;
}) {
  return (
    <section className="mb-12">
      <Heading
        level={2}
        className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl"
      >
        🔑 Public Keys
      </Heading>
      <p className="leading-relaxed mb-4 text-base md:text-lg">
        These Ed25519 public keys verify the identity of the official ACDS
        server at the endpoints listed in the{" "}
        <Link href="#infrastructure">Official ACDS Infrastructure</Link>{" "}
        section. The ascii-chat client automatically downloads and trusts these
        keys, establishing a secure connection to the ACDS server without
        requiring manual verification. You may also download and verify these
        keys yourself if preferred.
      </p>
      <p className="leading-relaxed mb-4 text-base md:text-lg">
        Keys are available at:
      </p>
      <ul className="leading-relaxed ml-0 pl-4 space-y-2">
        <li>
          <Link href="/key.pub">
            <code className="bg-gray-800 px-1 rounded">{baseUrl}/key.pub</code>
          </Link>{" "}
          (SSH)
        </li>
        <li>
          <Link href="/key.gpg">
            <code className="bg-gray-800 px-1 rounded">{baseUrl}/key.gpg</code>
          </Link>{" "}
          (GPG)
        </li>
      </ul>

      <Heading
        level={3}
        className="text-gray-200 mt-6 mb-2 text-xl md:text-2xl"
      >
        SSH Ed25519 Public Key
      </Heading>
      <p className="leading-relaxed mb-2 text-base md:text-lg">
        <strong>Fingerprint:</strong>
      </p>
      <PreCode>
        {sshFingerprint ||
          (sshKey ? "..." : "(set SSH_PUBLIC_KEY env var at build time)")}
      </PreCode>
      <p className="leading-relaxed mb-2 text-base md:text-lg">
        <strong>Public Key:</strong>
      </p>
      <PreCode style={{ minHeight: "60px" }}>
        {sshKey || "(SSH_PUBLIC_KEY env var not set at build time)"}
      </PreCode>
      <Button
        variant="secondary"
        href="/key.pub"
        download
        className="inline-block mt-2 mb-4"
      >
        ⬇ Download SSH Public Key
      </Button>

      <Heading
        level={3}
        className="text-gray-200 mt-6 mb-2 text-xl md:text-2xl"
      >
        GPG Ed25519 Public Key
      </Heading>
      <p className="leading-relaxed mb-2 text-base md:text-lg">
        <strong>Fingerprint:</strong>
      </p>
      <PreCode>
        {gpgFingerprint ||
          (gpgKey ? "..." : "(set GPG_PUBLIC_KEY env var at build time)")}
      </PreCode>
      <p className="leading-relaxed mb-2 text-base md:text-lg">
        <strong>Public Key:</strong>
      </p>
      <PreCode style={{ minHeight: "200px" }}>
        {gpgKey || "(GPG_PUBLIC_KEY env var not set at build time)"}
      </PreCode>
      <Button
        variant="secondary"
        href="/key.gpg"
        download
        className="inline-block mt-2 mb-4"
      >
        ⬇ Download GPG Public Key
      </Button>
    </section>
  );
}
