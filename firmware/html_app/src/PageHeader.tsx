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
    </>
  );
}

export default PageHeader;
