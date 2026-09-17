import type uPlot from "uplot";

// Data helpers shared by the history views. Kept apart from HistoryChart.tsx because a module
// that exports both components and plain functions breaks React Fast Refresh.
//
// Every series the firmware records has the same shape -- a 16-byte header giving
// baseTs/interval/record_size, then fixed-width slots -- so the pages differ only in which
// columns they chart and what the units are. See firmware/main/storage/history_format.h.

// The response from GET /api/history and GET /api/history/room|radiator. energyWh and cop are
// KIND_HOME only: the other kinds carry no power column, so the firmware omits the tail.
export type HistoryResponse = {
  storage: string;
  kind?: number;
  interval?: number;
  baseTs?: number;
  slots?: number;
  stride?: number;
  fields?: string[];
  points?: (number | null)[][];
  energyWh?: { heat?: number; electrical?: number };
  cop?: number | null;
  reason?: string;
};

export function todayISO(): string {
  // Local date, matching the firmware's local-day file naming.
  const d = new Date();
  const pad = (n: number) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
}

export function shiftDate(iso: string, days: number): string {
  const [y, m, d] = iso.split("-").map(Number);
  const dt = new Date(y, m - 1, d);
  dt.setDate(dt.getDate() + days);
  const pad = (n: number) => String(n).padStart(2, "0");
  return `${dt.getFullYear()}-${pad(dt.getMonth() + 1)}-${pad(dt.getDate())}`;
}

// uPlot wants column-major data: [xs, series0, series1, ...]. The API sends row-major
// points, so this picks the columns a chart needs, transposes, and rescales each into
// display units.
//
// Callers should useMemo this: Chart's effect compares `data` by identity, so a fresh array
// every render means the plot is destroyed and rebuilt every render.
export function toPlotData(
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
