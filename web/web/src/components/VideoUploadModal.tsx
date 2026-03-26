import { useCallback } from "react";
import { Dialog, DialogPanel, DialogTitle } from "@headlessui/react";
import { useDropzone } from "react-dropzone";

interface VideoUploadModalProps {
  isOpen: boolean;
  onClose: () => void;
  onFileSelect: (file: File) => void;
}

export function VideoUploadModal({
  isOpen,
  onClose,
  onFileSelect,
}: VideoUploadModalProps) {
  const onDrop = useCallback(
    (acceptedFiles: File[]) => {
      const file = acceptedFiles[0];
      if (file) {
        onFileSelect(file);
        onClose();
      }
    },
    [onFileSelect, onClose],
  );

  const { getRootProps, getInputProps, isDragActive } = useDropzone({
    onDrop,
    accept: {
      "video/*": [
        ".mp4",
        ".webm",
        ".mkv",
        ".avi",
        ".mov",
        ".flv",
        ".wmv",
        ".m4v",
        ".gif",
      ],
    },
    multiple: false,
  });

  return (
    <Dialog open={isOpen} onClose={onClose} className="relative z-50">
      <div className="fixed inset-0 bg-black/60" aria-hidden="true" />

      <div className="fixed inset-0 flex items-center justify-center p-4">
        <DialogPanel className="w-full max-w-md bg-terminal-0 border border-terminal-8 rounded-lg shadow-xl">
          <div className="p-6">
            <DialogTitle className="text-lg font-semibold text-terminal-fg mb-4">
              Upload Video
            </DialogTitle>

            <div
              {...getRootProps()}
              className={`border-2 border-dashed rounded-lg p-8 text-center cursor-pointer transition-colors ${
                isDragActive
                  ? "border-terminal-4 bg-terminal-4/10"
                  : "border-terminal-8 hover:border-terminal-7 hover:bg-terminal-bg/50"
              }`}
            >
              <input {...getInputProps()} />
              <div className="text-terminal-fg text-sm mb-2">
                {isDragActive
                  ? "Drop your video here"
                  : "Drag & drop a video file here"}
              </div>
              <div className="text-terminal-8 text-xs">
                or click to browse files
              </div>
              <div className="text-terminal-8 text-xs mt-3">
                MP4, WebM, MKV, AVI, MOV, GIF
              </div>
            </div>

            <button
              onClick={onClose}
              className="w-full mt-4 px-4 py-2 bg-terminal-8 text-terminal-fg rounded hover:bg-terminal-7 text-sm"
            >
              Cancel
            </button>
          </div>
        </DialogPanel>
      </div>
    </Dialog>
  );
}
