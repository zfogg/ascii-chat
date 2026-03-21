import { MirrorDemoWidget } from "../demo";
import { SITES } from "@ascii-chat/shared/utils";

export default function MiniMirrorDemo() {
  return (
    <section className="mb-12 sm:mb-16">
      <h2 className="text-2xl sm:text-3xl font-bold text-green-300 mb-4">
        🎬 Demo it right now
      </h2>
      <MirrorDemoWidget showHeader={false} />
      <p className="text-gray-600 text-xs mt-2 sm:text-right">
        Demo video:{" "}
        <a
          href="https://www.youtube.com/watch?v=9s1x9eHoEBE&t=1796"
          className="text-gray-500 hover:text-gray-400 underline"
          target="_blank"
          rel="noopener noreferrer"
        >
          ODESZA - THE FINALE - LIVE FROM THE GORGE
        </a>
      </p>
      <p className="text-gray-500 text-xs mt-1 sm:text-right">
        Want to play with this more? The web client{" "}
        <a
          href={`${SITES.WEB}/mirror`}
          className="text-cyan-400 hover:text-cyan-300 underline"
          target="_blank"
          rel="noopener noreferrer"
        >
          has a /mirror page
        </a>{" "}
        with all the options.
      </p>
    </section>
  );
}
