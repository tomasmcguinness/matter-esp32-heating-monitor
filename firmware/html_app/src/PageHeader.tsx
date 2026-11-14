import type { ReactNode } from "react";
import HomeStatus from "./HomeStatus";
import { useHome } from "./HomeContext";

// A tab's heading: the title on the left, the home's live status centred, and the page's action
// button, if it has one, on the right. Every top-level tab uses it, so the outdoor and indoor
// temperatures and the heat pump's mode are always in view.
//
function PageHeader({ title, children }: { title: ReactNode, children?: ReactNode }) {
  const home = useHome();

  return (
    <>
      <h1 className="page-header">
        <span className="page-header-title">{title}</span>
        <span className="page-header-status">
          {home && <HomeStatus
            outdoorTemperature={home.outdoorTemperature}
            indoorTemperature={home.averageInternalTemperature}
            heatingRunning={home.heatingRunning}
            dhwRunning={home.dhwRunning} />}
        </span>
        <span className="page-header-actions">{children}</span>
      </h1>
      <hr />
      {/* On every tab, because a card left unconverted is otherwise only a line in the serial log:
          those days can't be charted or totalled, and today's samples aren't being recorded if
          today's file is one of them. */}
      {!!home?.historyNeedsConversion && (
        <div className="alert alert-warning">
          {home.historyNeedsConversion} {home.historyNeedsConversion === 1 ? "day" : "days"} of
          history on the SD card {home.historyNeedsConversion === 1 ? "is" : "are"} in the old
          format and can't be read until the card is converted. Nothing is written to those days.
          Power the monitor off and follow "Converting the SD card" in firmware/CLAUDE.md.
        </div>
      )}
    </>
  );
}

export default PageHeader;
