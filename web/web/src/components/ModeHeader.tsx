import { useUrlState } from "../hooks/useUrlState";
import { useId, type ReactNode } from "react";
import { AnimatedPanel, PanelChevron } from "./AnimatedPanel";
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
  const [networkOpen, setNetworkOpen] = useUrlState("network", true);
  const networkId = useId();

  return (
    <>
      <DeviceSetupModal
        open={deviceSetupOpen}
        onClose={() => setDeviceSetupOpen(false)}
      />
      <AnimatedPanel open={showSettings}>{settingsPanel}</AnimatedPanel>
      {connectionPanel && (
        <section className="shrink-0 border-b border-terminal-8">
          <button
            type="button"
            aria-expanded={networkOpen}
            aria-controls={networkId}
            onClick={() => setNetworkOpen((open) => !open)}
            className="flex w-full items-center gap-2 px-4 py-3 text-sm cursor-pointer hover:bg-terminal-8/20"
          >
            <PanelChevron open={networkOpen} />
            Network settings
          </button>
          <AnimatedPanel open={networkOpen} id={networkId}>
            <div className="px-4 pt-1 pb-4">{connectionPanel}</div>
          </AnimatedPanel>
        </section>
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
