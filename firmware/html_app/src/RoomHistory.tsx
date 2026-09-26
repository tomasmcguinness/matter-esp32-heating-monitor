import { useCallback, useEffect, useMemo, useState } from "react";
import { NavLink, useParams } from "react-router";
import { Chart, DateToolbar } from "./HistoryChart";
import RadiatorChart from "./RadiatorChart";
import {
  ROOM_CHART_FIELDS,
  ROOM_COL,
  fieldsMatch,
  loadRadiatorSeries,
  roomTempByTs,
  toPlotData,
  todayISO,
  type HistoryResponse,
  type Radiator,
  type RadiatorSeries,
} from "./historyData";

// GET /api/history/room returns one local day of a room's recorded air temperature, and
// GET /api/history/radiator one day of a radiator's flow and return.
//
// Both records store measurements only -- see rec_room_t and rec_radiator_t in
// firmware/main/storage/history_format.h. Heat output, mean water temperature, dT and heat loss
// are all functions of these temperatures plus configuration in NVS, so they are derived when
// needed rather than frozen onto the card. That is why each record has a reserved tail that
// always reads null.

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
  const [radiators, setRadiators] = useState<Radiator[]>([]);
  const [radiatorData, setRadiatorData] = useState<RadiatorSeries[]>([]);
  const [ratedOutputs, setRatedOutputs] = useState<Map<number, number>>(new Map());
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

        if (!fieldsMatch(body, ROOM_CHART_FIELDS)) {
          // The record layout changed under us; charting by position would silently plot the
          // wrong quantity, so say so rather than draw something plausible and wrong.
          setError(`Unexpected field order from the device: ${body.fields?.join(", ")}`);
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

  // GET /api/rooms/:id carries the room's radiators inline, so this one request supplies both the
  // heading and the list of radiator series to load -- no extra call, and no second source of
  // truth for which radiators are in the room. A failure here leaves the room's own chart usable.
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
          setRadiators(
            (room.radiators ?? []).map((r: { radiatorId: number; name: string }) => ({
              radiatorId: r.radiatorId,
              name: r.name,
            }))
          );
        }
      })
      .catch(() => {
        /* heading falls back to the id, and the radiator charts are simply absent */
      });
    return () => {
      cancelled = true;
    };
  }, [id]);

  // The rated output at dT 50 is configuration, not history, and the room payload does not carry
  // it -- GET /api/radiators does, for every radiator in one request. Without it the charts still
  // draw flow and return, just no output line.
  useEffect(() => {
    let cancelled = false;
    fetch("/api/radiators")
      .then((r) => (r.ok ? r.json() : null))
      .then((list: { radiatorId: number; output: number }[] | null) => {
        if (!cancelled && list) {
          setRatedOutputs(new Map(list.map((r) => [r.radiatorId, r.output])));
        }
      })
      .catch(() => {
        /* the output line is simply absent */
      });
    return () => {
      cancelled = true;
    };
  }, []);

  // One radiator series at a time, on purpose. The device serves HTTP from a single task with a
  // small socket table and lru_purge_enable on, so a room with several radiators all streaming
  // 800 points at once can evict the web UI's WebSocket. Each result is appended as it lands, so
  // the charts fill in progressively rather than the page waiting on the slowest one.
  useEffect(() => {
    setRadiatorData([]);

    if (id === null || radiators.length === 0) {
      return;
    }

    let cancelled = false;

    (async () => {
      for (const radiator of radiators) {
        const loaded = await loadRadiatorSeries(radiator, date, 800);

        // Bail rather than append: the date has moved on, and this is last request's day.
        if (cancelled) {
          return;
        }
        setRadiatorData((previous) => [...previous, loaded]);
      }
    })();

    return () => {
      cancelled = true;
    };
  }, [id, date, radiators]);

  const points = useMemo(() => data?.points ?? [], [data]);
  const hasPoints = points.length > 0;

  // Memoised because Chart rebuilds the plot whenever this array changes identity.
  const tempData = useMemo(
    () => (hasPoints ? toPlotData(points, [ROOM_COL.currentTemp], [0.01]) : null),
    [points, hasPoints]
  );

  const stats = useMemo(() => (hasPoints ? temperatureStats(points) : null), [points, hasPoints]);

  // Output needs the room's air temperature at the same instant, so the room's own series doubles
  // as the lookup the radiator charts join against.
  const roomTemps = useMemo(() => roomTempByTs(points), [points]);

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

      {/* Outside the hasPoints block on purpose: a room with no temperature sensor bound still
          has radiators worth charting, and they are what explains the room's line when it does.
          Suppressed when the card is missing, so one absent card is one message rather than one
          per radiator. */}
      {!loading && !error && data?.storage !== "unavailable" && radiators.length > 0 && (
        <>
          <h2>Radiators</h2>

          {radiatorData.map((series) => (
            <RadiatorChart
              key={series.radiatorId}
              series={series}
              date={date}
              ratedW={ratedOutputs.get(series.radiatorId)}
              roomTempByTs={roomTemps}
            />
          ))}

          {radiatorData.length < radiators.length && <p>Loading&hellip;</p>}
        </>
      )}

      <NavLink to={`/rooms/${id}`}>Back</NavLink>
    </>
  );
}

export default RoomHistory;
