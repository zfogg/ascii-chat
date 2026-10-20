import { useState } from "react";
import { VideoUploadModal } from "./VideoUploadModal";

export function UploadFile({
  onFileSelect,
}: {
  onFileSelect: (file: File) => void;
}) {
  const [open, setOpen] = useState(false);
  return (
    <>
      <button
        type="button"
        onClick={() => setOpen(true)}
        className="px-4 py-2 bg-terminal-5 text-terminal-bg rounded hover:bg-terminal-13 text-sm font-medium"
      >
        Upload File
      </button>
      <VideoUploadModal
        isOpen={open}
        onClose={() => setOpen(false)}
        onFileSelect={onFileSelect}
        allowImages
      />
    </>
  );
}
