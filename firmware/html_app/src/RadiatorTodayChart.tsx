import { useEffect, useMemo, useState } from "react";
import { ChartCard } from "./HistoryChart";
import RadiatorChart from "./RadiatorChart";
import {
  ROOM_CHART_FIELDS,
  fieldsMatch,
  loadRadiatorSeries,
  roomTempByTs,
  type HistoryResponse,
  type RadiatorSeries,
} from "./historyData";

// RadiatorChart supplies the card, so every state here has to supply its own to match -- otherwise
// the panel vanishes when there is nothing to draw and the page shifts under it.
const CARD_TITLE = "Readings today";

// 200 points, as on the room page: this is a glance, and every point is read off an SD card by a
// web server with a single task.
const POINTS = 200;

// Today's recorded flow and return on the radiator page, with the air temperature of the room the
// radiator is in and the output derived from all three.
//
// Like RoomTodayChart, the requests carry **no `date`**: omitted means the device's own local day,
// which is what the files on the card are named after.
function RadiatorTodayChart({
  radiatorId,
  name,
  ratedW,
  roomId,
}: {
  radiatorId: number;
  name: string;
  ratedW: number | undefined;
  roomId: number | null;
}) {
  const [radiator, setRadiator] = useState<RadiatorSeries | null>(null);
  const [roomPoints, setRoomPoints] = useState<(number | null)[][]>([]);
  const [loading, setLoading] = useState<boolean>(true);

  useEffect(() => {
    let cancelled = false;

    // One request after the other, not in parallel. The device serves HTTP from a single task with
    // a small socket table and lru_purge_enable on -- see RoomHistory.
    const load = async () => {
      setLoading(true);
      setRoomPoints([]);

      const loaded = await loadRadiatorSeries({ radiatorId, name }, undefined, POINTS);

      if (cancelled) {
        return;
      }

      setRadiator(loaded);

      if (roomId !== null && !loaded.error && !loaded.storageUnavailable) {
        // A failure here costs only the room line and the output derived from it; the radiator's
        // own flow and return still draw.
        try {
          const response = await fetch(`/api/history/room?id=${roomId}&points=${POINTS}`);

          if (response.ok) {
            const body: HistoryResponse = await response.json();

            if (!cancelled && fieldsMatch(body, ROOM_CHART_FIELDS)) {
              setRoomPoints(body.points ?? []);
            }
          }
        } catch {
          /* the room line is simply absent */
        }
      }

      if (!cancelled) {
        setLoading(false);
      }
    };

    load();

    return () => {
      cancelled = true;
    };
  }, [radiatorId, name, roomId]);

  const roomTemps = useMemo(() => roomTempByTs(roomPoints), [roomPoints]);

  // Everything short of a chart is a muted line rather than an alert: this is secondary content
  // on a page whose live values are already showing, so a missing card should not shout.
  const muted = (message: string) => (
    <ChartCard title={CARD_TITLE}>
      <span className="text-muted">{message}</span>
    </ChartCard>
  );

  if (loading || radiator === null) {
    return muted("Loading…");
  }

  if (radiator.storageUnavailable) {
    return muted("No SD card is fitted, so nothing is being recorded.");
  }

  if (radiator.error) {
    // A dateless request is a 400 until the first SNTP sync, because before it the device's clock
    // is in 1970 and it cannot name today's file. That is "nothing recorded yet", not a fault.
    return muted(
      radiator.status === 400 ? "Nothing has been recorded yet." : "History could not be loaded."
    );
  }

  if (radiator.points.length === 0) {
    return muted("No readings recorded yet today.");
  }

  return (
    <RadiatorChart
      series={radiator}
      date="today"
      ratedW={ratedW}
      roomTempByTs={roomTemps}
      showRoomTemp
      title={CARD_TITLE}
    />
  );
}

export default RadiatorTodayChart;
