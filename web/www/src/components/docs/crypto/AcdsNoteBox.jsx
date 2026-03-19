import TrackedLink from "../../TrackedLink";
import { SITES } from "@ascii-chat/shared/utils";

export default function AcdsNoteBox() {
  return (
    <div className="mb-12 bg-purple-900/20 border border-purple-700/50 rounded-lg p-6">
      <p className="text-gray-300">
        <strong className="text-purple-300">Note:</strong> Looking for
        ascii-chat Discovery Service (ACDS) cryptography details or public
        keys? See the{" "}
        <TrackedLink
          href={SITES.DISCOVERY}
          label="Crypto - ACDS Docs"
          target="_blank"
          rel="noopener noreferrer"
          className="text-cyan-400 hover:text-cyan-300 transition-colors underline"
        >
          ACDS website
        </TrackedLink>{" "}
        for discovery service crypto architecture.
      </p>
    </div>
  );
}
