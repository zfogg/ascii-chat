import type { ReactNode } from "react";
import { PageControlBar, type PageControlBarProps } from "./PageControlBar";

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
  return (
    <>
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
          compactVerticalSpacing={!!connectionPanel}
        />
      </div>
    </>
  );
}
