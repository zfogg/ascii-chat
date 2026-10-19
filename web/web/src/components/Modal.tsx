import type { ReactNode } from "react";
import { Dialog, DialogBackdrop, DialogPanel } from "@headlessui/react";
import "./Modal.css";

export { DialogTitle as ModalTitle } from "@headlessui/react";

interface ModalProps {
  open: boolean;
  onClose: () => void;
  children: ReactNode;
  className?: string;
  backdropClassName?: string;
}

export function Modal({
  open,
  onClose,
  children,
  className = "",
  backdropClassName = "bg-black/75 backdrop-blur-sm",
}: ModalProps) {
  return (
    <Dialog open={open} onClose={onClose} className="modal">
      <DialogBackdrop className={`modal-backdrop ${backdropClassName}`} />
      <div className="modal-viewport">
        <div className="modal-layout">
          <DialogPanel className={`modal-panel ${className}`}>
            {children}
          </DialogPanel>
        </div>
      </div>
    </Dialog>
  );
}
