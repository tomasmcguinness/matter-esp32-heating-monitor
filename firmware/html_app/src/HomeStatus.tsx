import type { ReactNode } from "react";
import Temperature from "./Temperature";

type HomeStatusProps = {
  outdoorTemperature: number | null | undefined;
  indoorTemperature: number | null | undefined;
  heatingRunning: boolean | null | undefined;
  dhwRunning: boolean | null | undefined;
};

function StatusItem({ title, className, icon, children }: { title: string, className?: string, icon: ReactNode, children: ReactNode }) {
  return (
    <span className={`d-inline-flex align-items-center gap-1 ${className ?? ""}`} title={title}>
      {icon}
      {children}
    </span>
  );
}

const iconProps = { xmlns: "http://www.w3.org/2000/svg", width: 20, height: 20, fill: "currentColor", viewBox: "0 0 16 16" };

const outdoorIcon = (
  <svg {...iconProps} className="bi bi-cloud-sun">
    <path d="M7 8a3.5 3.5 0 0 1 3.5 3.555.5.5 0 0 0 .624.492A1.503 1.503 0 0 1 13 13.5a1.5 1.5 0 0 1-1.5 1.5H3a2 2 0 1 1 .1-3.998.5.5 0 0 0 .51-.375A3.5 3.5 0 0 1 7 8m4.473 3a4.5 4.5 0 0 0-8.72-.99A3 3 0 0 0 3 16h8.5a2.5 2.5 0 0 0 0-5z" />
    <path d="M10.5 1.5a.5.5 0 0 0-1 0v1a.5.5 0 0 0 1 0zm3.743 1.964a.5.5 0 1 0-.707-.707l-.708.707a.5.5 0 0 0 .708.708zm-7.779-.707a.5.5 0 0 0-.707.707l.707.708a.5.5 0 1 0 .708-.708zm1.734 3.374a2 2 0 1 1 3.296 2.198q.3.423.516.898a3 3 0 1 0-4.84-3.225q.529.017 1.028.129m4.484 4.074c.6.215 1.125.59 1.522 1.072a.5.5 0 0 0 .039-.742l-.707-.707a.5.5 0 0 0-.854.377M14.5 6.5a.5.5 0 0 0 0 1h1a.5.5 0 0 0 0-1z" />
  </svg>
);

const indoorIcon = (
  <svg {...iconProps} className="bi bi-house">
    <path d="M8.707 1.5a1 1 0 0 0-1.414 0L.646 8.146a.5.5 0 0 0 .708.708L2 8.207V13.5A1.5 1.5 0 0 0 3.5 15h9a1.5 1.5 0 0 0 1.5-1.5V8.207l.646.647a.5.5 0 0 0 .708-.708L13 5.793V2.5a.5.5 0 0 0-.5-.5h-1a.5.5 0 0 0-.5.5v1.293zM13 7.207V13.5a.5.5 0 0 1-.5.5h-9a.5.5 0 0 1-.5-.5V7.207l5-5z" />
  </svg>
);

const heatingIcon = (
  <svg {...iconProps} className="bi bi-fire">
    <path d="M8 16c3.314 0 6-2 6-5.5 0-1.5-.5-4-2.5-6 .25 1.5-1.25 2-1.25 2C11 4 9 .5 6 0c.357 2 .5 4-2 6-1.25 1-2 2.729-2 4.5C2 14 4.686 16 8 16m0-1c-1.657 0-3-1-3-2.75 0-.75.25-2 1.25-3C6.125 10 7 10.5 7 10.5c-.375-1.25.5-3.25 2-3.5-.179 1-.25 2 1 3 .625.5 1 1.364 1 2.25C11 14 9.657 15 8 15" />
  </svg>
);

const hotWaterIcon = (
  <svg {...iconProps} className="bi bi-droplet-fill">
    <path d="M8 16a6 6 0 0 0 6-6c0-1.655-1.122-2.904-2.432-4.362C10.254 4.176 8.75 2.503 8 0c0 0-6 5.686-6 10a6 6 0 0 0 6 6M6.646 4.646l.708.708c-.29.29-1.128 1.311-1.907 2.87l-.894-.448c.82-1.641 1.717-2.753 2.093-3.13" />
  </svg>
);

const idleIcon = (
  <svg {...iconProps} className="bi bi-pause-circle">
    <path d="M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16" />
    <path d="M5 6.25a1.25 1.25 0 1 1 2.5 0v3.5a1.25 1.25 0 1 1-2.5 0zm3.5 0a1.25 1.25 0 1 1 2.5 0v3.5a1.25 1.25 0 1 1-2.5 0z" />
  </svg>
);

const unknownIcon = (
  <svg {...iconProps} className="bi bi-question-circle">
    <path d="M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16" />
    <path d="M5.255 5.786a.237.237 0 0 0 .241.247h.825c.138 0 .248-.113.266-.25.09-.656.54-1.134 1.342-1.134.686 0 1.314.343 1.314 1.168 0 .635-.374.927-.965 1.371-.673.489-1.206 1.06-1.168 1.987l.003.217a.25.25 0 0 0 .25.246h.811a.25.25 0 0 0 .25-.25v-.105c0-.718.273-.927 1.01-1.486.609-.463 1.244-.977 1.244-2.056 0-1.511-1.276-2.241-2.673-2.241-1.267 0-2.655.59-2.75 2.286m1.557 5.763c0 .533.425.927 1.01.927.609 0 1.028-.394 1.028-.927 0-.552-.42-.94-1.029-.94-.584 0-1.009.388-1.009.94" />
  </svg>
);

// What the heat pump is doing right now. Hot water wins when both are on, as it does on the
// history mode strip (modeSpans in historyData.ts). Both are null with no heat pump picked, or
// before it has reported, which is "Unknown" rather than "Idle".
//
function HeatPumpMode({ heatingRunning, dhwRunning }: { heatingRunning: boolean | null | undefined, dhwRunning: boolean | null | undefined }) {
  if (dhwRunning) {
    return <StatusItem title="The heat pump is making hot water" className="text-primary" icon={hotWaterIcon}>Hot water</StatusItem>;
  }

  if (heatingRunning) {
    return <StatusItem title="The heat pump is heating" className="text-warning" icon={heatingIcon}>Heating</StatusItem>;
  }

  if (heatingRunning === false || dhwRunning === false) {
    return <StatusItem title="The heat pump is idle" className="text-body-secondary" icon={idleIcon}>Idle</StatusItem>;
  }

  return <StatusItem title="The heat pump's mode is not known" className="text-body-secondary" icon={unknownIcon}>Unknown</StatusItem>;
}

function HomeStatus({ outdoorTemperature, indoorTemperature, heatingRunning, dhwRunning }: HomeStatusProps) {
  return (
    <span className="d-inline-flex flex-wrap align-items-center gap-3 fs-5 fw-normal">
      <StatusItem title="Outdoor temperature" icon={outdoorIcon}><Temperature>{outdoorTemperature}</Temperature></StatusItem>
      <StatusItem title="Average indoor temperature" icon={indoorIcon}><Temperature>{indoorTemperature}</Temperature></StatusItem>
      <HeatPumpMode heatingRunning={heatingRunning} dhwRunning={dhwRunning} />
    </span>
  );
}

export default HomeStatus;
