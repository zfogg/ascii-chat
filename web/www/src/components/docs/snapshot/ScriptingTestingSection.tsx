import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function ScriptingTestingSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-green-400">
        🚀 Scripting and Testing
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          Batch Processing Images
        </Heading>
        <CodeBlock language="bash">
          {
            '# Convert all images in directory\nfor img in *.jpg; do\n  echo "Converting $img..."\n  ascii-chat mirror --file "$img" --snapshot > "ascii_${img}.txt"\ndone\n\n# Convert with specific palette and dimensions\nfor img in *.png; do\n  ascii-chat mirror --file "$img" --snapshot \\\n    --palette blocks --width 100 --height 50 \\\n    > "ascii_${img}.txt"\ndone'
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          Integration Testing
        </Heading>
        <CodeBlock language="bash">
          {
            '#!/bin/bash\n# Integration test: verify entire stack works\n\necho "Testing webcam..."\nif ! ascii-chat mirror -S -D 2 > /tmp/local.txt 2>&1; then\n  echo "✗ Webcam test failed"\n  exit 1\nfi\necho "✓ Webcam working"\n\necho "Testing server connection..."\nif ! ascii-chat client example.com -S -D 3 > /tmp/remote.txt 2>&1; then\n  echo "✗ Network test failed"\n  exit 1\nfi\necho "✓ Network connection working"\n\necho "Testing encrypted connection..."\nif ! ascii-chat client example.com --key ~/.ssh/id_ed25519 -S -D 3 > /tmp/encrypted.txt 2>&1; then\n  echo "✗ Encryption test failed"\n  exit 1\nfi\necho "✓ Encryption working"\n\necho "All tests passed!"\nexit 0'
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-3">
          Video Frame Extraction
        </Heading>
        <CodeBlock language="bash">
          {
            '#!/bin/bash\n# Extract frames from video at intervals\n\nVIDEO="$1"\nFRAMES_PER_SECOND=1\n\nif [ ! -f "$VIDEO" ]; then\n  echo "Usage: $0 <video-file>"\n  exit 1\nfi\n\nmkdir -p frames\n\n# Get video duration\nDURATION=$(ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1:nk=1 "$VIDEO")\nFRAMES=$(echo "$DURATION * $FRAMES_PER_SECOND" | bc)\n\necho "Extracting ~$FRAMES frames from $VIDEO"\n\nfor i in $(seq 0 $((1/$FRAMES_PER_SECOND)) $(echo "$DURATION - 1" | bc)); do\n  TIME=$(printf "%02d:%02d:%06.3f" $((${i%.*}/60/60)) $(((${i%.*}/60)%60)) $(bc -l <<< "$i - int($i) + int($i)"))\n  ascii-chat mirror --file "$VIDEO" --seek "$i" -S -D 1 \\\n    --color-mode mono --palette minimal > "frames/frame_$(printf "%06d" $i).txt"\ndone\n\necho "Frames saved to frames/ directory"'
          }
        </CodeBlock>
      </div>
    </section>
  );
}
