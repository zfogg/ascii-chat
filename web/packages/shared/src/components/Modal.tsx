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
  rootClassName?: string;
}

export function Modal({
  open,
  onClose,
  children,
  className = "",
  backdropClassName = "bg-black/75 backdrop-blur-sm",
  rootClassName = "",
}: ModalProps) {
  return (
    <Dialog open={open} onClose={onClose} className={`modal ${rootClassName}`}>
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
