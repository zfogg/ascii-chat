import { Heading, Link } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";

export default function HeroHeader({ handleLinkClick }) {
  return (
    <header className="text-center mb-12 pb-8 border-b-2 border-gray-700">
      <Heading level={1} className="mb-2 text-blue-400 text-3xl md:text-4xl">
        🔍 ascii-chat Discovery Service
      </Heading>
      <p className="text-gray-400 text-lg md:text-xl m-0">
        Official Public Keys
      </p>
      <p className="text-gray-400 text-lg md:text-xl m-0">
        Session signalling for{" "}
        <Link
          href={SITES.MAIN}
          onClick={() =>
            handleLinkClick(SITES.MAIN, "ascii-chat website (header)")
          }
        >
          ascii-chat
        </Link>
      </p>
    </header>
  );
}
