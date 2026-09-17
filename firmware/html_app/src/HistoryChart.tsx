import { useEffect, useRef } from "react";
import uPlot from "uplot";
import "uplot/dist/uPlot.min.css";
import { shiftDate, todayISO } from "./historyData";

export type ChartProps = {
  title: string;
  data: uPlot.AlignedData | null;
  // Compared by identity in the effect below, so keep this a module-level constant or memoise it.
  series: { label: string; stroke: string }[];
  unit: string;
  decimals?: number;
};

export function Chart({ title, data, series, unit, decimals = 1 }: ChartProps) {
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
          title,
          width: holder.current!.clientWidth,
          height: 260,
          // Gaps: the API sends null for slots with no reading, and this keeps uPlot from
          // drawing a line across an outage.
          series: [
            { label: "Time" },
            ...series.map((s) => ({
              label: s.label,
              stroke: s.stroke,
              width: 1.5,
              spanGaps: false,
              value: (_u: uPlot, v: number | null) =>
                v === null ? "--" : `${v.toFixed(decimals)} ${unit}`,
            })),
          ],
          axes: [{}, { label: unit }],
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
  }, [data, title, series, unit, decimals]);

  return <div ref={holder} style={{ marginBottom: "20px" }} />;
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
