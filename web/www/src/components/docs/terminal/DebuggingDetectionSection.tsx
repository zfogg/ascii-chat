import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function DebuggingDetectionSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-pink-400">
        🔍 Debugging & Detection
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-pink-300 mb-3">
          Show Terminal Capabilities
        </Heading>
        <p className="docs-paragraph">
          Display detected terminal capabilities and configuration:
        </p>
        <CodeBlock language="bash">ascii-chat --show-capabilities</CodeBlock>
        <p className="text-gray-400 text-sm mt-2">
          Output includes: color support, Unicode support, terminal size, isatty
          status, environment variables, and detected TERM type.
        </p>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-purple-300 mb-3">
          Debug Display Issues
        </Heading>
        <div className="space-y-3">
          <div className="card-standard accent-purple">
            <Heading level={4} className="text-purple-300 font-semibold mb-2">
              Colors Look Wrong
            </Heading>
            <ol className="list-decimal list-inside text-gray-300 text-sm space-y-1">
              <li>
                Run <code>--show-capabilities</code>
              </li>
              <li>
                Check TERM: <code>echo $TERM</code>
              </li>
              <li>
                Force a color mode: <code>--color-mode 256</code>
              </li>
              <li>
                Over SSH, set <code>export TERM=xterm-256color</code>
              </li>
            </ol>
          </div>

          <div className="card-standard accent-purple">
            <Heading level={4} className="text-purple-300 font-semibold mb-2">
              Unicode Characters Broken
            </Heading>
            <ol className="list-decimal list-inside text-gray-300 text-sm space-y-1">
              <li>
                Check locale: <code>locale</code>
              </li>
              <li>
                Ensure UTF-8 locale: <code>export LANG=en_US.UTF-8</code>
              </li>
              <li>
                Force UTF-8: <code>--utf8 true</code>
              </li>
            </ol>
          </div>

          <div className="card-standard accent-purple">
            <Heading level={4} className="text-purple-300 font-semibold mb-2">
              Dimensions Wrong
            </Heading>
            <ol className="list-decimal list-inside text-gray-300 text-sm space-y-1">
              <li>
                Check detected size: <code>--show-capabilities</code>
              </li>
              <li>
                Try explicit size: <code>-x 120 -y 40</code>
              </li>
              <li>
                When piped, set: <code>COLUMNS=120 ROWS=40</code>
              </li>
            </ol>
          </div>
        </div>
      </div>
    </section>
  );
}
