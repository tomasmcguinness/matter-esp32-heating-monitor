import { useCallback, useEffect, useMemo, useState } from "react";
import { NavLink, useParams } from "react-router";
import { Chart, DateToolbar } from "./HistoryChart";
import { toPlotData, todayISO, type HistoryResponse } from "./historyData";

// GET /api/history/room returns one local day of a room's recorded air temperature.
//
// The room record stores measurements only -- see rec_room_t in
// firmware/main/storage/history_format.h. Heat output, mean water temperature and heat loss are
// all functions of these temperatures plus configuration in NVS, so they are derived when needed
// rather than frozen onto the card, which is why there is one real column here and three
// reserved ones that always read null.
const ROOM_FIELDS = ["currentTempC100", "reserved0", "reserved1", "reserved2"];

// Column index within a point row. Element 0 is the timestamp, so a field's index is its
// record position plus one.
const ROOM_COL = { currentTemp: 1 } as const;

// Module-level, because Chart compares its series prop by identity.
const TEMP_SERIES = [{ label: "Temperature", stroke: "#0275d8" }];

// A room id keys the room's history directory on the card, and the firmware validates it as
// 0-255. Checking here as well keeps a hand-typed URL from reaching the API at all.
function parseRoomId(raw: string | undefined): number | null {
  if (raw === undefined || !/^\d+$/.test(raw)) {
    return null;
  }
  const id = Number.parseInt(raw, 10);
  return id >= 0 && id <= 255 ? id : null;
}

type Stats = { min: number; max: number; mean: number } | null;

// Min/max/mean over the day, ignoring slots with no reading. The API's buckets are already
// averaged across `stride` slots, so the mean is a mean of means rather than of raw slots --
// close enough for a headline figure, and it matches what the chart shows.
function temperatureStats(points: (number | null)[][]): Stats {
  let min = Infinity;
  let max = -Infinity;
  let sum = 0;
  let n = 0;

  for (const p of points) {
    const v = p[ROOM_COL.currentTemp];
    if (v === null || v === undefined) {
      continue;
    }
    if (v < min) min = v;
    if (v > max) max = v;
    sum += v;
    n++;
  }

  return n === 0 ? null : { min: min / 100, max: max / 100, mean: sum / n / 100 };
}

function RoomHistory() {
  const { roomId } = useParams();
  const id = parseRoomId(roomId);

  const [date, setDate] = useState<string>(todayISO());
  const [data, setData] = useState<HistoryResponse | null>(null);
  const [name, setName] = useState<string | null>(null);
  const [loading, setLoading] = useState<boolean>(true);
  const [error, setError] = useState<string | null>(null);

  const load = useCallback(
    async (forRoom: number, forDate: string) => {
      setLoading(true);
      setError(null);
      try {
        const response = await fetch(
          `/api/history/room?id=${forRoom}&date=${forDate}&points=800`
        );
        if (!response.ok) {
          setError(`Request failed: ${response.status}`);
          setData(null);
          return;
        }
        const body: HistoryResponse = await response.json();

        if (body.fields && body.fields.join(",") !== ROOM_FIELDS.join(",")) {
          // The record layout changed under us; charting by position would silently plot the
          // wrong quantity, so say so rather than draw something plausible and wrong.
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
    },
    []
  );

  useEffect(() => {
    if (id === null) {
      setLoading(false);
      return;
    }
    load(id, date);
  }, [id, date, load]);

  // The name is only for the heading, so a failure here leaves the chart usable.
  useEffect(() => {
    if (id === null) {
      return;
    }
    let cancelled = false;
    fetch(`/api/rooms/${id}`)
      .then((r) => (r.ok ? r.json() : null))
      .then((room) => {
        if (!cancelled && room) {
          setName(room.name);
        }
      })
      .catch(() => {
        /* heading falls back to the id */
      });
    return () => {
      cancelled = true;
    };
  }, [id]);

  const points = useMemo(() => data?.points ?? [], [data]);
  const hasPoints = points.length > 0;

  // Memoised because Chart rebuilds the plot whenever this array changes identity.
  const tempData = useMemo(
    () => (hasPoints ? toPlotData(points, [ROOM_COL.currentTemp], [0.01]) : null),
    [points, hasPoints]
  );

  const stats = useMemo(() => (hasPoints ? temperatureStats(points) : null), [points, hasPoints]);

  if (id === null) {
    return (
      <>
        <h1>Room history</h1>
        <hr />
        <div className="alert alert-danger">
          &ldquo;{roomId}&rdquo; is not a valid room id.
        </div>
        <NavLink to="/rooms">Back</NavLink>
      </>
    );
  }

  return (
    <>
      <h1>{name ?? `Room ${id}`} history</h1>
      <hr />

      <DateToolbar date={date} setDate={setDate} />

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
              <div className="card-header">Coldest</div>
              <div className="card-body">
                <h3 className="card-title">
                  {stats ? `${stats.min.toFixed(1)} °C` : "-"}
                </h3>
              </div>
            </div>
            <div className="card">
              <div className="card-header">Warmest</div>
              <div className="card-body">
                <h3 className="card-title">
                  {stats ? `${stats.max.toFixed(1)} °C` : "-"}
                </h3>
              </div>
            </div>
            <div className="card">
              <div className="card-header">Mean</div>
              <div className="card-body">
                <h3 className="card-title">
                  {stats ? `${stats.mean.toFixed(1)} °C` : "-"}
                </h3>
              </div>
            </div>
            <div className="card">
              <div className="card-header">Sampled every</div>
              <div className="card-body">
                <h3 className="card-title">{data?.interval}s</h3>
              </div>
            </div>
          </div>

          {/* Gaps are slots with no reading -- the sensor unbound, unreachable, or the device
              off -- rather than a temperature of zero. */}
          <Chart title="Temperature" data={tempData} series={TEMP_SERIES} unit="&deg;C" />
        </>
      )}

      <NavLink to={`/rooms/${id}`}>Back</NavLink>
    </>
  );
}

export default RoomHistory;
