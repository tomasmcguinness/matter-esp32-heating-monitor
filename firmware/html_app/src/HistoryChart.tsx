import { useEffect, useRef } from "react";
import uPlot from "uplot";
import "uplot/dist/uPlot.min.css";
import { shiftDate, todayISO, type ModeSpan } from "./historyData";

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
  // Draws each line through a moving average of its points, as a curve. For slow quantities like
  // water and air temperature, where a probe's last-digit jitter otherwise hides the trend.
  smooth?: boolean;
  // When the heat pump was heating and when it was making hot water, drawn as a strip along the
  // bottom of the plot. Compared by identity like `data`, so memoise it.
  modes?: ModeSpan[];
};

// The strip's colours, and its height in CSS pixels. Idle time is left blank.
const MODE_COLOURS = { heating: "#fd7e14", hotWater: "#6f42c1" } as const;
const MODE_LABELS = { heating: "Heating", hotWater: "Hot water" } as const;
const MODE_BAR_PX = 8;

// Fills each span across the bottom edge of the plot area. Positions come from the x scale, so the
// strip stays aligned with the lines and the synced cursor whatever the chart's own points are.
function drawModes(u: uPlot, modes: ModeSpan[]) {
  const { ctx, bbox } = u;
  const barHeight = MODE_BAR_PX * devicePixelRatio;
  const right = bbox.left + bbox.width;

  ctx.save();

  for (const span of modes) {
    const x0 = Math.max(bbox.left, u.valToPos(span.from, "x", true));
    const x1 = Math.min(right, u.valToPos(span.to, "x", true));

    if (x1 > x0) {
      ctx.fillStyle = MODE_COLOURS[span.mode];
      ctx.fillRect(x0, bbox.top + bbox.height - barHeight, x1 - x0, barHeight);
    }
  }

  ctx.restore();
}

// Points either side of each one in the moving average. At the 200 points a day the today charts
// ask for, that is about half an hour of readings per point.
const SMOOTH_RADIUS = 2;

// Each value is replaced by the mean of the non-null values within SMOOTH_RADIUS of it. A null
// stays null, so an outage is still drawn as a gap rather than bridged by its neighbours.
function smoothData(data: uPlot.AlignedData): uPlot.AlignedData {
  const [xs, ...ys] = data;

  const smoothed = ys.map((column) =>
    Array.from(column, (value, i) => {
      if (value === null || value === undefined) {
        return null;
      }

      let sum = 0;
      let count = 0;

      for (let j = Math.max(0, i - SMOOTH_RADIUS); j <= Math.min(column.length - 1, i + SMOOTH_RADIUS); j++) {
        const neighbour = column[j];

        if (neighbour !== null && neighbour !== undefined) {
          sum += neighbour;
          count++;
        }
      }

      return sum / count;
    })
  );

  return [xs, ...smoothed] as uPlot.AlignedData;
}

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
  smooth = false,
  modes,
}: ChartProps) {
  const holder = useRef<HTMLDivElement>(null);
  const plot = useRef<uPlot | null>(null);

  useEffect(() => {
    if (!holder.current || !data) {
      return;
    }

    const drawn = smooth ? smoothData(data) : data;

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
                ...(smooth ? { paths: uPlot.paths.spline!() } : {}),
                // The legend reads from the unsmoothed data, so hovering shows what was recorded
                // rather than an average of it.
                value: (_u: uPlot, _v: number | null, seriesIdx: number, idx: number | null) => {
                  const v = idx === null ? null : data[seriesIdx][idx];
                  return v === null || v === undefined ? "--" : `${v.toFixed(seriesDecimals)} ${seriesUnit}`;
                },
              };
            }),
          ],
          // uPlot creates a scale on demand from a series or axis that names one, but declaring it
          // keeps that an explicit part of the config rather than a behaviour to rely on.
          scales: rightUnit ? { y2: {} } : undefined,
          hooks: modes && modes.length > 0 ? { draw: [(u) => drawModes(u, modes)] } : undefined,
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
        drawn,
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
  }, [data, series, unit, decimals, rightUnit, rightDecimals, smooth, modes]);

  return (
    <ChartCard title={title}>
      <div ref={holder} />
      {modes && modes.length > 0 && (
        <div className="text-muted small" style={{ textAlign: "center" }}>
          {(["heating", "hotWater"] as const).map((mode) => (
            <span key={mode} style={{ margin: "0 8px" }}>
              <span
                style={{
                  display: "inline-block",
                  width: "12px",
                  height: `${MODE_BAR_PX}px`,
                  marginRight: "4px",
                  background: MODE_COLOURS[mode],
                }}
              />
              {MODE_LABELS[mode]}
            </span>
          ))}
        </div>
      )}
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
