import { useCallback, useEffect, useRef, useState } from "react";
import uPlot from "uplot";
import "uplot/dist/uPlot.min.css";

// GET /api/history returns one local day of the home's recorded values: fixed-cadence slots
// with nulls where no reading was recorded, plus the day's integrated energy.
//
// History records quantities, not sensors, so these columns stay continuous across a sensor
// being replaced or re-paired.
//
// Field order matches rec_home_t in firmware/main/storage/history_format.h. The firmware
// also sends the names in "fields", but the chart needs to know which is which, so the
// order is relied on here and asserted against that list below. The reserved columns are
// placeholders for quantities not yet recorded; they always read null.
const FIELDS = [
  "heatPowerW",
  "elecPowerW",
  "flowTempC100",
  "returnTempC100",
  "flowLph",
  "outdoorTempC100",
  "internalTempC100",
  "copX100",
  "dhwRunning",
  "elecVoltageDv",
  "elecCurrentCa",
  "reserved0",
  "reserved1",
  "reserved2",
];

// Column index within a point row. Element 0 is the timestamp, so a field's index is its
// record position plus one.
const COL = {
  heatPower: 1,
  elecPower: 2,
  flowTemp: 3,
  returnTemp: 4,
  outdoorTemp: 6,
  internalTemp: 7,
  cop: 8,
} as const;

type HistoryResponse = {
  storage: string;
  interval?: number;
  baseTs?: number;
  slots?: number;
  fields?: string[];
  points?: (number | null)[][];
  energyWh?: { heat?: number; electrical?: number };
  cop?: number | null;
  reason?: string;
};

function todayISO(): string {
  // Local date, matching the firmware's local-day file naming.
  const d = new Date();
  const pad = (n: number) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
}

function shiftDate(iso: string, days: number): string {
  const [y, m, d] = iso.split("-").map(Number);
  const dt = new Date(y, m - 1, d);
  dt.setDate(dt.getDate() + days);
  const pad = (n: number) => String(n).padStart(2, "0");
  return `${dt.getFullYear()}-${pad(dt.getMonth() + 1)}-${pad(dt.getDate())}`;
}

// uPlot wants column-major data: [xs, series0, series1, ...]. The API sends row-major
// points, so this picks the columns a chart needs, transposes, and rescales each into
// display units.
function toPlotData(
  points: (number | null)[][],
  columns: number[],
  scales: number[]
): uPlot.AlignedData {
  const xs = new Array<number>(points.length);
  const cols = columns.map(() => new Array<number | null>(points.length));

  for (let i = 0; i < points.length; i++) {
    xs[i] = points[i][0] as number;
    for (let c = 0; c < columns.length; c++) {
      const v = points[i][columns[c]];
      cols[c][i] = v === null || v === undefined ? null : v * scales[c];
    }
  }
  return [xs, ...cols] as unknown as uPlot.AlignedData;
}

type ChartProps = {
  title: string;
  data: uPlot.AlignedData | null;
  series: { label: string; stroke: string }[];
  unit: string;
  decimals?: number;
};

function Chart({ title, data, series, unit, decimals = 1 }: ChartProps) {
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

const POWER_SERIES = [
  { label: "Heat out", stroke: "#d9534f" },
  { label: "Electricity in", stroke: "#0275d8" },
];

const TEMP_SERIES = [
  { label: "Flow", stroke: "#d9534f" },
  { label: "Return", stroke: "#5bc0de" },
  { label: "Outdoor", stroke: "#5cb85c" },
  { label: "Indoor", stroke: "#f0ad4e" },
];

const COP_SERIES = [{ label: "COP", stroke: "#9354d9" }];

function History() {
  const [date, setDate] = useState<string>(todayISO());
  const [data, setData] = useState<HistoryResponse | null>(null);
  const [loading, setLoading] = useState<boolean>(true);
  const [error, setError] = useState<string | null>(null);

  const load = useCallback(async (forDate: string) => {
    setLoading(true);
    setError(null);
    try {
      const response = await fetch(`/api/history?date=${forDate}&points=800`);
      if (!response.ok) {
        setError(`Request failed: ${response.status}`);
        setData(null);
        return;
      }
      const body: HistoryResponse = await response.json();

      if (body.fields && body.fields.join(",") !== FIELDS.join(",")) {
        // The record layout changed under us; charting by position would silently plot the
        // wrong quantities, so say so rather than draw something plausible and wrong.
        setError(`Unexpected field order from the device: ${body.fields.join(", ")}`);
        setData(null);
        return;
      }
      setData(body);
    } catch (e) {
      setError(String(e));
      setData(null);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    load(date);
  }, [date, load]);

  const points = data?.points ?? [];
  const hasPoints = points.length > 0;

  // Watts stay as watts; the 0.01 fields become degrees and whole COP units.
  const powerData = hasPoints
    ? toPlotData(points, [COL.heatPower, COL.elecPower], [1, 1])
    : null;
  const tempData = hasPoints
    ? toPlotData(
        points,
        [COL.flowTemp, COL.returnTemp, COL.outdoorTemp, COL.internalTemp],
        [0.01, 0.01, 0.01, 0.01]
      )
    : null;
  const copData = hasPoints ? toPlotData(points, [COL.cop], [0.01]) : null;

  const heatKwh = (data?.energyWh?.heat ?? 0) / 1000;
  const elecKwh = (data?.energyWh?.electrical ?? 0) / 1000;

  return (
    <>
      <h1>History</h1>
      <hr />

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

      {loading && <p>Loading&hellip;</p>}

      {error && <div className="alert alert-danger">{error}</div>}

      {!loading && !error && data?.storage === "unavailable" && (
        <div className="alert alert-warning">
          No SD card is fitted, so nothing is being recorded. Fit a FAT32-formatted card and
          restart the device.
        </div>
      )}

      {!loading && !error && data?.storage === "ok" && !hasPoints && (
        <div className="alert alert-info">No readings recorded on {date}.</div>
      )}

      {!loading && !error && hasPoints && (
        <>
          <div className="card-group" style={{ marginBottom: "20px" }}>
            <div className="card">
              <div className="card-header">Heat out</div>
              <div className="card-body">
                <p className="card-title">
                  <h3>{heatKwh.toFixed(1)} kWh</h3>
                </p>
              </div>
            </div>
            <div className="card">
              <div className="card-header">Electricity in</div>
              <div className="card-body">
                <p className="card-title">
                  <h3>{elecKwh.toFixed(1)} kWh</h3>
                </p>
              </div>
            </div>
            <div className="card">
              <div className="card-header">COP</div>
              <div className="card-body">
                <p className="card-title">
                  <h3>{data?.cop != null ? data.cop.toFixed(2) : "-"}</h3>
                </p>
              </div>
            </div>
            <div className="card">
              <div className="card-header">Sampled every</div>
              <div className="card-body">
                <p className="card-title">
                  <h3>{data?.interval}s</h3>
                </p>
              </div>
            </div>
          </div>

          <Chart title="Power" data={powerData} series={POWER_SERIES} unit="W" />
          <Chart title="Temperatures" data={tempData} series={TEMP_SERIES} unit="&deg;C" />
          {/* Instantaneous COP, unlike the card above, which is the day's integrated figure.
              Gaps are the heat source being off rather than missing data. */}
          <Chart title="COP" data={copData} series={COP_SERIES} unit="" decimals={2} />
        </>
      )}
    </>
  );
}

export default History;
