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

// The leading columns each page charts, named as history_api.c's field_names_for() names them.
// A page plots by column position, so it has to check the device agrees about the order first --
// otherwise a record layout change silently plots the wrong quantity against a plausible axis.
//
// Only the charted prefix is listed, not the whole record. Every record in history_format.h ends
// in a reserved tail so a quantity can be added later without moving record_size, and the columns
// are named precisely so "a field added to the reserved tail shows up as a named column without
// the UI needing to be rebuilt". Comparing the full list would reject exactly that case.
export const ROOM_CHART_FIELDS = ["currentTempC100"];
export const RADIATOR_CHART_FIELDS = ["flowTempC100", "returnTempC100"];

// Column indices within a point row. Element 0 is the timestamp, so a field's index is its
// record position plus one.
export const ROOM_COL = { currentTemp: 1 } as const;
export const RADIATOR_COL = { flowTemp: 1, returnTemp: 2 } as const;

// True when the device's column order starts with the fields the caller is about to chart.
//
// A response with no `fields` passes: the firmware answers a day it has no file for with
// {"storage":"ok","points":[]} and no `fields` or `interval`, which is an empty day rather than a
// disagreement about the layout.
export function fieldsMatch(body: HistoryResponse, expected: string[]): boolean {
  if (!body.fields) {
    return true;
  }
  return expected.every((name, i) => body.fields![i] === name);
}

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

export type Radiator = { radiatorId: number; name: string };

// Room air temperature by timestamp, for deriving output. Joined on the timestamp rather than the
// array index: all three series share one sampling interval, so slot i is the same instant in
// every file written that day, but `stride` is computed per file from that file's slot count, so a
// radiator whose file started mid-day buckets differently from the room's.
export type RoomTempByTs = Map<number, number>;

export function roomTempByTs(points: (number | null)[][]): RoomTempByTs {
  const byTs: RoomTempByTs = new Map();
  for (const point of points) {
    const temp = point[ROOM_COL.currentTemp];
    if (temp !== null && temp !== undefined) {
      byTs.set(point[0] as number, temp);
    }
  }
  return byTs;
}

// Heat output in watts, derived the way update_radiator_outputs() in
// firmware/main/managers/calculations_manager.cpp derives the live figure:
//
//   mwt    = (flow + return) / 2
//   deltaT = |mwt - room air|
//   output = rated / (50 / deltaT) ^ 1.3        -- the rating is quoted at dT 50
//
// Derived here rather than recorded because it is a function of the two stored temperatures plus
// configuration in NVS, and a stored copy would have frozen in whatever the arithmetic was on the
// day. firmware/CLAUDE.md is explicit that the deriving belongs off the device.
//
// The firmware's guards are reproduced: a non-positive flow, return or room reading means it
// declines to calculate, which is a gap here rather than a zero. Unlike the firmware this is not
// truncated to a uint16, so the line is exact where the live value on the room page is rounded.
export function radiatorOutputW(
  flowC100: number | null,
  returnC100: number | null,
  roomC100: number | undefined,
  ratedW: number
): number | null {
  if (flowC100 === null || returnC100 === null || roomC100 === undefined) {
    return null;
  }
  if (flowC100 <= 0 || returnC100 <= 0 || roomC100 <= 0) {
    return null;
  }

  const meanWaterTemp = (flowC100 + returnC100) / 2 / 100;
  const deltaT = Math.abs(meanWaterTemp - roomC100 / 100);

  // The firmware divides by deltaT, so no temperature difference yields an infinite factor and
  // therefore no output. Same answer, without the division.
  return deltaT === 0 ? 0 : ratedW * Math.pow(deltaT / 50, 1.3);
}

// One radiator's day, as loaded. `points` empty with no error means the card holds no file for
// that day -- a radiator added since, or the device off.
export type RadiatorSeries = Radiator & {
  points: (number | null)[][];
  error: string | null;
  // Set when the device answered but is not recording: no SD card fitted.
  storageUnavailable?: boolean;
  // The HTTP status of a failed request, so a caller can tell "no clock yet" (400) from a fault.
  status?: number;
};

// Loads one radiator's day. With no `forDate` the request carries no date at all, which means the
// device's own local day -- see RoomTodayChart for why that beats sending the browser's date.
export async function loadRadiatorSeries(
  radiator: Radiator,
  forDate: string | undefined,
  points: number
): Promise<RadiatorSeries> {
  try {
    const dateParam = forDate === undefined ? "" : `&date=${forDate}`;
    const response = await fetch(
      `/api/history/radiator?id=${radiator.radiatorId}${dateParam}&points=${points}`
    );

    if (!response.ok) {
      return {
        ...radiator,
        points: [],
        error: `Request failed: ${response.status}`,
        status: response.status,
      };
    }

    const body: HistoryResponse = await response.json();

    if (body.storage === "unavailable") {
      return { ...radiator, points: [], error: null, storageUnavailable: true };
    }

    if (!fieldsMatch(body, RADIATOR_CHART_FIELDS)) {
      return {
        ...radiator,
        points: [],
        error: `Unexpected field order from the device: ${body.fields?.join(", ")}`,
      };
    }

    return { ...radiator, points: body.points ?? [], error: null };
  } catch (e) {
    return { ...radiator, points: [], error: String(e) };
  }
}
