import { useEffect, useMemo, useState } from "react";
import { useNavigate } from "react-router";
import { Chart, ChartCard } from "./HistoryChart";
import {
  ROOM_CHART_FIELDS,
  fieldsMatch,
  toPlotData,
  type HistoryResponse,
} from "./historyData";

// Module-level, because Chart compares its series prop by identity.
const TODAY_SERIES = [{ label: "Temperature", stroke: "#0275d8" }];

// Chart supplies the card, so every state here has to supply its own to match -- otherwise the
// panel vanishes when there is nothing to draw and the page shifts under whatever is below it.
const CARD_TITLE = "Temperature today";

// Column index within a point row. Element 0 is the timestamp, so a field's index is its
// record position plus one.
const CURRENT_TEMP_COL = 1;

// Today's recorded room temperature on the room page, and the way through to the room's full
// history.
//
// The request deliberately carries **no `date`**: omitted means the device's own local day, which
// is what the files on the card are named after. Sending the browser's date instead would ask for
// the wrong day whenever a phone and the board disagree about where midnight is.
//
// 200 points rather than the history page's 800. This is a glance, and every point is read off an
// SD card by a web server with a single task.
function RoomTodayChart({ roomId }: { roomId: number }) {
  const navigate = useNavigate();

  const [data, setData] = useState<HistoryResponse | null>(null);
  const [loading, setLoading] = useState<boolean>(true);
  const [notRecording, setNotRecording] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;

    const load = async () => {
      setLoading(true);
      setNotRecording(null);
      try {
        const response = await fetch(`/api/history/room?id=${roomId}&points=200`);

        if (cancelled) {
          return;
        }

        if (!response.ok) {
          // A dateless request is a 400 until the first SNTP sync, because before it the device's
          // clock is in 1970 and it cannot name today's file. That is "nothing recorded yet", not
          // a fault worth an alert on a page that is otherwise working.
          setNotRecording(
            response.status === 400
              ? "Nothing has been recorded yet."
              : `History unavailable (${response.status}).`
          );
          setData(null);
          return;
        }

        const body: HistoryResponse = await response.json();

        if (!fieldsMatch(body, ROOM_CHART_FIELDS)) {
          setNotRecording("The device's history columns are not in the expected order.");
          setData(null);
          return;
        }

        setData(body);
      } catch {
        if (!cancelled) {
          setNotRecording("History could not be loaded.");
          setData(null);
        }
      } finally {
        if (!cancelled) {
          setLoading(false);
        }
      }
    };

    load();

    return () => {
      cancelled = true;
    };
  }, [roomId]);

  const points = useMemo(() => data?.points ?? [], [data]);
  const hasPoints = points.length > 0;

  // Memoised because Chart rebuilds the plot whenever this array changes identity.
  const plotData = useMemo(
    () => (hasPoints ? toPlotData(points, [CURRENT_TEMP_COL], [0.01]) : null),
    [points, hasPoints]
  );

  const openHistory = () => navigate(`/rooms/${roomId}/history`);

  // Everything short of a chart is a muted line rather than an alert: this is secondary content
  // on a page whose live values are already showing, so a missing card should not shout.
  const muted = (message: string) => (
    <ChartCard title={CARD_TITLE}>
      <span className="text-muted">{message}</span>
    </ChartCard>
  );

  if (loading) {
    return muted("Loading\u2026");
  }

  if (notRecording) {
    return muted(notRecording);
  }

  if (data?.storage === "unavailable") {
    return muted("No SD card is fitted, so nothing is being recorded.");
  }

  if (!hasPoints) {
    return muted("No readings recorded yet today.");
  }

  return (
    <div
      role="link"
      tabIndex={0}
      title="Open this room's full history"
      style={{ cursor: "pointer" }}
      onClick={openHistory}
      // uPlot installs its own cursor listeners on the overlay but does not stop propagation, so
      // the click reaches this wrapper. Keyboard handling is here rather than inherited because
      // this is a navigation control, unlike the table rows elsewhere that use a bare onClick.
      onKeyDown={(e) => {
        if (e.key === "Enter" || e.key === " ") {
          e.preventDefault();
          openHistory();
        }
      }}
    >
      <Chart title={CARD_TITLE} data={plotData} series={TODAY_SERIES} unit="&deg;C" />
    </div>
  );
}

export default RoomTodayChart;
