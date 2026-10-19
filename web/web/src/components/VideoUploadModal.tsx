import { MAX_MEDIA_FILE_SIZE } from "../utils/mediaUpload";
import { useCallback, useState } from "react";
import { Modal, ModalTitle } from "./Modal";
import { useDropzone } from "react-dropzone";

interface VideoUploadModalProps {
  isOpen: boolean;
  allowImages?: boolean;
  onClose: () => void;
  onFileSelect: (file: File) => void;
}

export function VideoUploadModal({
  isOpen,
  allowImages = false,
  onClose,
  onFileSelect,
}: VideoUploadModalProps) {
  const [error, setError] = useState("");
  const onDrop = useCallback(
    (acceptedFiles: File[]) => {
      const file = acceptedFiles[0];
      if (file) {
        setError("");
        onFileSelect(file);
        onClose();
      }
    },
    [onFileSelect, onClose],
  );

  const { getRootProps, getInputProps, isDragActive } = useDropzone({
    onDrop,
    accept: {
      ...(allowImages
        ? {
            "image/*": [
              ".gif",
              ".png",
              ".jpg",
              ".jpeg",
              ".webp",
              ".avif",
              ".bmp",
            ],
          }
        : {}),
      "video/*": [
        ".mp4",
        ".webm",
        ".mkv",
        ".avi",
        ".mov",
        ".flv",
        ".wmv",
        ".m4v",
        ".mpeg",
        ".mpg",
        ".ogv",
        ".ts",
        ".m2ts",
      ],
    },
    multiple: false,
    maxSize: MAX_MEDIA_FILE_SIZE,
    onDropRejected: (rejections) =>
      setError(
        rejections.some(({ errors }) =>
          errors.some(({ code }) => code === "file-too-large"),
        )
          ? "Files must be 8 GB or smaller."
          : "Choose one video or image file at a time.",
      ),
    onFileDialogOpen: () => setError(""),
  });

  return (
    <Modal
      open={isOpen}
      onClose={onClose}
      backdropClassName="bg-black/60"
      className="max-w-md bg-terminal-0 border border-terminal-8 rounded-lg shadow-xl"
    >
      <div className="p-6">
        <ModalTitle className="text-lg font-semibold text-terminal-fg mb-4">
          Upload File
        </ModalTitle>

        <div
          {...getRootProps()}
          className={`border-2 border-dashed rounded-lg p-8 text-center cursor-pointer transition-colors ${
            isDragActive
              ? "border-terminal-4 bg-terminal-4/10"
              : "border-terminal-8 hover:border-terminal-7 hover:bg-terminal-bg/50"
          }`}
        >
          <input {...getInputProps()} aria-label="Choose media file" />
          <div className="text-terminal-fg text-sm mb-2">
            {isDragActive
              ? "Drop your media file here"
              : "Drag & drop a media file here"}
          </div>
          <div className="text-terminal-8 text-xs">
            or click to browse files
          </div>
          <div className="text-terminal-8 text-xs mt-3">
            MP4, WebM, MKV, AVI, MOV, MPEG, WMV, FLV
            {allowImages && "  /  GIF, PNG, JPEG, WebP"}
          </div>
        </div>

        {allowImages && (
          <p className="text-xs text-terminal-8 mt-3">
            Files open locally; connected sessions share their video frames.
            Less common video formats are converted in your browser before
            playback. Maximum file size: 8 GB.
          </p>
        )}
        {error && (
          <p role="alert" className="text-terminal-1 mt-3">
            {error}
          </p>
        )}
        <button
          onClick={onClose}
          className="w-full mt-4 px-4 py-2 bg-terminal-8 text-terminal-fg rounded hover:bg-terminal-7 text-sm"
        >
          Cancel
        </button>
      </div>
    </Modal>
  );
}
