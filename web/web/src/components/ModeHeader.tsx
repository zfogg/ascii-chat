import { useUrlState } from "../hooks/useUrlState";
import { type ReactNode } from "react";
import { PageControlBar, type PageControlBarProps } from "./PageControlBar";
import { DeviceSetupModal } from "./DeviceSetupModal";

interface ModeHeaderProps {
  showSettings: boolean;
  settingsPanel?: ReactNode;
  connectionPanel?: ReactNode;
  controlBar: PageControlBarProps;
}

/**
 * Shared page header shell for client, discovery, and mirror modes.
 *
 * Each connecting mode supplies its own connection form and details while the
 * shell keeps their spacing, settings panel, status row, and controls aligned.
 */
export function ModeHeader({
  showSettings,
  settingsPanel,
  connectionPanel,
  controlBar,
}: ModeHeaderProps) {
  const [deviceSetupOpen, setDeviceSetupOpen] = useUrlState("devices", false);

  return (
    <>
      <DeviceSetupModal
        open={deviceSetupOpen}
        onClose={() => setDeviceSetupOpen(false)}
      />
      {showSettings && settingsPanel}
      {connectionPanel && (
        <div className="px-4 py-4 border-b border-terminal-8">
          {connectionPanel}
        </div>
      )}
      <div
        className={
          connectionPanel ? "px-4 py-4 border-b border-terminal-8" : undefined
        }
      >
        <PageControlBar
          {...controlBar}
          onDeviceSetupClick={() => setDeviceSetupOpen(true)}
          compactVerticalSpacing={!!connectionPanel}
        />
      </div>
    </>
  );
}
