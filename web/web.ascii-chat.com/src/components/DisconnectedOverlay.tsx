interface DisconnectedOverlayProps {
  isDisconnected: boolean;
}

export function DisconnectedOverlay({ isDisconnected }: DisconnectedOverlayProps) {
  if (!isDisconnected) return null;

  return (
    <div className="absolute inset-0 flex items-center justify-center bg-black/40 rounded pointer-events-none">
      <div className="text-5xl font-bold text-red-500 drop-shadow-lg">
        DISCONNECTED
      </div>
    </div>
  );
}
