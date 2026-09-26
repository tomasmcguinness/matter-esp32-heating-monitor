import { useCallback, useEffect, useState } from "react";
import { Chart, DateToolbar } from "./HistoryChart";
import { toPlotData, todayISO, type HistoryResponse } from "./historyData";

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
  "averageInternalTempC100",
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
  averageInternalTemp: 7,
  cop: 8,
} as const;

const POWER_SERIES = [
  { label: "Heat out", stroke: "#d9534f" },
  { label: "Electricity in", stroke: "#0275d8" },
];

const TEMP_SERIES = [
  { label: "Flow", stroke: "#d9534f" },
  { label: "Return", stroke: "#5bc0de" },
  { label: "Outdoor", stroke: "#5cb85c" },
  { label: "Average Indoor", stroke: "#f0ad4e" },
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
        [COL.flowTemp, COL.returnTemp, COL.outdoorTemp, COL.averageInternalTemp],
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
