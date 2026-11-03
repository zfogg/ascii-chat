import { Modal, ModalTitle } from "@ascii-chat/shared/components/Modal";
import { useUrlState } from "../hooks/useUrlState";
import "./ModeInstructions.css";

const instructions = {
  mirror: {
    title: "Using Mirror",
    steps: [
      "Choose your camera and microphone in Devices, and allow browser access when prompted.",
      "Start the preview to see your video rendered as ASCII. You can also open a media file or share your screen.",
      "Use the controls and settings to adjust the picture and audio.",
    ],
  },
  client: {
    title: "Using Client",
    steps: [
      "Enter the server address and port in Network settings, then select Connect.",
      "Choose your camera and microphone in Devices, and allow browser access when prompted.",
      "Use the controls to manage your media and adjust the ASCII display.",
    ],
  },
  discovery: {
    title: "Using Discovery",
    steps: [
      "Enter a session name and select Join session, or select Create session and share the generated name.",
      "If the session has a password, enter it before joining.",
      "Choose your camera and microphone in Devices, and allow browser access when prompted.",
    ],
  },
} as const;

export type InstructionMode = keyof typeof instructions;

export function ModeInstructions({ mode }: { mode: InstructionMode }) {
  const [open, setOpen] = useUrlState("help", false);
  const { title, steps } = instructions[mode];
  return (
    <>
      <button
        type="button"
        aria-label={`Open ${mode} instructions`}
        onClick={() => setOpen(true)}
        className="inline-flex h-8 w-8 items-center justify-center rounded-full border border-terminal-8 text-terminal-fg hover:border-terminal-cyan hover:text-terminal-cyan focus-visible:outline-2 focus-visible:outline-terminal-cyan"
      >
        ?
      </button>
      <Modal
        open={open}
        rootClassName="mode-instructions-dialog"
        onClose={() => setOpen(false)}
        className="max-w-lg rounded border border-terminal-8 bg-terminal-0 p-6 text-terminal-fg shadow-xl"
      >
        <ModalTitle className="text-xl font-bold text-terminal-cyan">
          {title}
        </ModalTitle>
        <ol className="my-5 list-decimal space-y-3 pl-5">
          {steps.map((step) => (
            <li key={step}>{step}</li>
          ))}
        </ol>
        <button
          type="button"
          onClick={() => setOpen(false)}
          className="rounded border border-terminal-8 px-4 py-2 hover:border-terminal-cyan"
        >
          Close
        </button>
      </Modal>
    </>
  );
}
