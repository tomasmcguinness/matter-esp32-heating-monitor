import { useMemo } from "react";
import type uPlot from "uplot";
import { Chart, ChartCard } from "./HistoryChart";
import { RADIATOR_COL, radiatorOutputW, type RadiatorSeries, type RoomTempByTs } from "./historyData";

// Module-level, because Chart compares its series prop by identity. The radiator colours are the
// ones History.tsx already uses for the heat meter's flow and return, and the room's is the one
// RoomTodayChart uses for a room's temperature.
const FLOW = { label: "Flow", stroke: "#d9534f" };
const RETURN = { label: "Return", stroke: "#5bc0de" };
const ROOM = { label: "Room", stroke: "#0275d8" };

// Output goes on the right-hand axis: it is watts against the temperatures' degrees.
const OUTPUT = { label: "Output", stroke: "#5cb85c", axis: "right" as const };

const RADIATOR_SERIES = [FLOW, RETURN];
const RADIATOR_SERIES_WITH_OUTPUT = [FLOW, RETURN, OUTPUT];
const RADIATOR_SERIES_WITH_ROOM = [FLOW, RETURN, ROOM];
const RADIATOR_SERIES_WITH_ROOM_AND_OUTPUT = [FLOW, RETURN, ROOM, OUTPUT];

// One radiator's chart: flow and return, plus derived output on the right-hand axis when the
// radiator's rating and the room's temperature are both known.
//
// `showRoomTemp` adds the room's air temperature as a line of its own. The radiator page wants it,
// since the room is what the radiator is heating; the room history page does not, because it
// already charts the room's temperature directly above.
//
// Its own component so the plot data can be memoised per radiator -- a useMemo cannot live in a
// loop in the parent -- and so the radiator page and the room history page share one chart.
function RadiatorChart({
  series,
  date,
  ratedW,
  roomTempByTs,
  showRoomTemp = false,
  title,
}: {
  series: RadiatorSeries;
  date: string;
  ratedW: number | undefined;
  roomTempByTs: RoomTempByTs;
  showRoomTemp?: boolean;
  title?: string;
}) {
  const chartTitle = title ?? series.name;

  // A room line is only worth drawing when there is a room series to draw it from.
  const withRoom = showRoomTemp && roomTempByTs.size > 0;

  const plotData = useMemo(() => {
    if (series.points.length === 0) {
      return null;
    }

    // Built by hand rather than with toPlotData() because output is computed per point, not
    // picked from a recorded column, and the room's temperature comes from another series.
    const xs: number[] = [];
    const flow: (number | null)[] = [];
    const ret: (number | null)[] = [];
    const room: (number | null)[] = [];
    const output: (number | null)[] = [];

    for (const point of series.points) {
      const ts = point[0] as number;
      const f = point[RADIATOR_COL.flowTemp] ?? null;
      const r = point[RADIATOR_COL.returnTemp] ?? null;
      const roomTemp = roomTempByTs.get(ts);

      xs.push(ts);
      flow.push(f === null ? null : f / 100);
      ret.push(r === null ? null : r / 100);

      if (withRoom) {
        room.push(roomTemp === undefined ? null : roomTemp / 100);
      }

      if (ratedW !== undefined) {
        output.push(radiatorOutputW(f, r, roomTemp, ratedW));
      }
    }

    const columns: (number | null)[][] = [xs, flow, ret];
    if (withRoom) {
      columns.push(room);
    }
    if (ratedW !== undefined) {
      columns.push(output);
    }
    return columns as unknown as uPlot.AlignedData;
  }, [series.points, ratedW, roomTempByTs, withRoom]);

  if (series.error) {
    return <ChartCard title={chartTitle}>{series.error}</ChartCard>;
  }

  if (plotData === null) {
    return (
      <ChartCard title={chartTitle}>
        <span className="text-muted">No readings recorded on {date}.</span>
      </ChartCard>
    );
  }

  // Without a rating there is no output to draw, so the right axis is left off entirely rather
  // than labelled against an empty line.
  if (ratedW === undefined) {
    return (
      <Chart
        title={chartTitle}
        data={plotData}
        series={withRoom ? RADIATOR_SERIES_WITH_ROOM : RADIATOR_SERIES}
        unit="&deg;C"
      />
    );
  }

  return (
    <Chart
      title={chartTitle}
      data={plotData}
      series={withRoom ? RADIATOR_SERIES_WITH_ROOM_AND_OUTPUT : RADIATOR_SERIES_WITH_OUTPUT}
      unit="&deg;C"
      rightUnit="W"
    />
  );
}

export default RadiatorChart;
