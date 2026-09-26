import { useEffect, useRef } from "react";
import uPlot from "uplot";
import "uplot/dist/uPlot.min.css";
import { shiftDate, todayISO } from "./historyData";

export type ChartSeries = {
  label: string;
  stroke: string;
  // Series on the right-hand axis, for a quantity that cannot share the left one's scale --
  // watts against degrees, say. Everything else shares the left axis.
  axis?: "right";
};

// Every chart joins this group, so hovering one draws the cursor at the same time on the others
// and they can be read against each other. Only one page is mounted at a time, so a single
// app-wide key is enough. uPlot syncs on the x value rather than the pixel, so charts whose data
// starts at different times still line up.
const CURSOR_SYNC_KEY = "history";

export type ChartProps = {
  title: string;
  data: uPlot.AlignedData | null;
  // Compared by identity in the effect below, so keep this a module-level constant or memoise it.
  series: ChartSeries[];
  unit: string;
  decimals?: number;
  // Supply these only when some series sets axis: "right".
  rightUnit?: string;
  rightDecimals?: number;
};

// Every chart sits in a card with its title as the header, so a page of charts reads as a stack of
// panels rather than a run of loose plots. Exported because a chart's empty and error states need
// the same box -- a radiator with no readings should still occupy its panel rather than collapsing
// the page around it.
export function ChartCard({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div className="card" style={{ marginBottom: "20px" }}>
      <div className="card-header">{title}</div>
      <div className="card-body">{children}</div>
    </div>
  );
}

export function Chart({
  title,
  data,
  series,
  unit,
  decimals = 1,
  rightUnit,
  rightDecimals = 0,
}: ChartProps) {
  const holder = useRef<HTMLDivElement>(null);
  const plot = useRef<uPlot | null>(null);

  useEffect(() => {
    if (!holder.current || !data) {
      return;
    }

    // uPlot sizes itself once, so it is rebuilt on data change and on resize rather than
    // being asked to reflow.
    const build = () => {
      plot.current?.destroy();
      plot.current = new uPlot(
        {
          // No `title` here: ChartCard's header carries it, and uPlot would draw a second one.
          width: holder.current!.clientWidth,
          height: 260,
          // setSeries is off because the charts on a page carry different series: toggling one in
          // a legend must not hide whatever happens to sit at the same index in the others.
          cursor: { sync: { key: CURSOR_SYNC_KEY, setSeries: false } },
          // Gaps: the API sends null for slots with no reading, and this keeps uPlot from
          // drawing a line across an outage.
          series: [
            { label: "Time" },
            ...series.map((s) => {
              const right = s.axis === "right";
              const seriesUnit = right ? rightUnit ?? "" : unit;
              const seriesDecimals = right ? rightDecimals : decimals;

              return {
                label: s.label,
                stroke: s.stroke,
                width: 1.5,
                spanGaps: false,
                scale: right ? "y2" : "y",
                value: (_u: uPlot, v: number | null) =>
                  v === null ? "--" : `${v.toFixed(seriesDecimals)} ${seriesUnit}`,
              };
            }),
          ],
          // uPlot creates a scale on demand from a series or axis that names one, but declaring it
          // keeps that an explicit part of the config rather than a behaviour to rely on.
          scales: rightUnit ? { y2: {} } : undefined,
          axes: [
            {},
            { label: unit },
            // Only drawn when something is on it. Its grid is off so the two axes' gridlines
            // don't overlay each other at unrelated values.
            ...(rightUnit
              ? [{ label: rightUnit, scale: "y2", side: 1, grid: { show: false } }]
              : []),
          ],
        },
        data,
        holder.current!
      );
    };

    build();
    window.addEventListener("resize", build);
    return () => {
      window.removeEventListener("resize", build);
      plot.current?.destroy();
      plot.current = null;
    };
  }, [data, series, unit, decimals, rightUnit, rightDecimals]);

  return (
    <ChartCard title={title}>
      <div ref={holder} />
    </ChartCard>
  );
}

type DateToolbarProps = {
  date: string;
  setDate: (iso: string) => void;
};

// Previous / picker / Next / Today. Next is disabled on today: the device cannot have recorded
// tomorrow, and ISO dates compare correctly as strings.
export function DateToolbar({ date, setDate }: DateToolbarProps) {
  return (
    <div className="btn-toolbar" style={{ marginBottom: "20px", gap: "10px" }}>
      <button className="btn btn-outline-secondary" onClick={() => setDate(shiftDate(date, -1))}>
        &larr; Previous
      </button>
      <input
        type="date"
        className="form-control"
        style={{ maxWidth: "200px" }}
        value={date}
        onChange={(e) => setDate(e.target.value)}
      />
      <button
        className="btn btn-outline-secondary"
        onClick={() => setDate(shiftDate(date, 1))}
        disabled={date >= todayISO()}
      >
        Next &rarr;
      </button>
      <button className="btn btn-outline-secondary" onClick={() => setDate(todayISO())}>
        Today
      </button>
    </div>
  );
}
