import { Heading, Link } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";

export default function SecuritySection({
  handleLinkClick,
}: {
  handleLinkClick: (url: string, text: string) => void;
}) {
  return (
    <section className="mb-12">
      <Heading
        level={2}
        className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl"
      >
        🔒 Security
      </Heading>
      <p className="leading-relaxed mb-4 text-base md:text-lg">
        The discovery service uses the same crypto protocol and code as the
        client/server. You can find more about the crypto protocol in the{" "}
        <Link
          href={SITES.CRYPTO_DOCS}
          onClick={() =>
            handleLinkClick(SITES.CRYPTO_DOCS, "Crypto docs (security)")
          }
        >
          ascii-chat docs
        </Link>
        . See the{" "}
        <Link
          href={SITES.MAIN + "/man1#SECURITY"}
          onClick={() =>
            handleLinkClick(
              SITES.MAIN + "/man1#SECURITY",
              "Man page SECURITY section",
            )
          }
        >
          man page
        </Link>{" "}
        for all discovery-service mode crypto flags.
      </p>
    </section>
  );
}
