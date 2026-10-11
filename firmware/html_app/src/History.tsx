import { useCallback, useEffect, useMemo, useState } from "react";
import { Chart, DateToolbar } from "./HistoryChart";
import {
  modeSpans,
  toPlotData,
  todayISO,
  type HistoryResponse,
  type ModeEnergy,
  type TotalsResponse,
} from "./historyData";

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
  "heatingRunning",
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

// A COP is energy out over energy in for the slots it covers. A dash means there was no
// electricity to divide by -- a day with no hot water run, or one recorded before the heat pump's
// mode was.
function CopCard({ title, cop }: { title: string; cop: number | null | undefined }) {
  return (
    <div className="card">
      <div className="card-header">{title}</div>
      <div className="card-body">
        <p className="card-title">
          <h3>{cop != null ? cop.toFixed(2) : "-"}</h3>
        </p>
      </div>
    </div>
  );
}

function EnergyCard({ title, kwh }: { title: string; kwh: number | null }) {
  return (
    <div className="card">
      <div className="card-header">{title}</div>
      <div className="card-body">
        <p className="card-title">
          <h3>{kwh != null ? `${kwh.toFixed(1)} kWh` : "-"}</h3>
        </p>
      </div>
    </div>
  );
}

// The heat a mode delivered, or null where its COP card shows a dash: no electricity was recorded
// in that mode, so there was no run to speak of or the day predates the mode being logged.
function modeHeatKwh(mode: ModeEnergy | undefined): number | null {
  return mode && mode.electricalWh > 0 ? mode.heatWh / 1000 : null;
}

function History() {
  const [date, setDate] = useState<string>(todayISO());
  const [data, setData] = useState<HistoryResponse | null>(null);
  const [loading, setLoading] = useState<boolean>(true);
  const [error, setError] = useState<string | null>(null);
  const [totals, setTotals] = useState<TotalsResponse | null>(null);

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

  // Every recorded day summed, so independent of the date being viewed and fetched once. A
  // failure costs only those cards.
  useEffect(() => {
    let cancelled = false;
    fetch("/api/history/totals")
      .then((r) => (r.ok ? r.json() : null))
      .then((body: TotalsResponse | null) => {
        if (!cancelled) {
          setTotals(body);
        }
      })
      .catch(() => {
        /* the all-days cards are simply absent */
      });
    return () => {
      cancelled = true;
    };
  }, []);

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

  // Memoised because Chart rebuilds the plot whenever this array changes identity.
  const modes = useMemo(() => modeSpans(data), [data]);

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

      {totals?.storage === "ok" && !!totals.days && (
        <>
          <div className="card-group" style={{ marginBottom: "5px" }}>
            <CopCard title="Heating COP, all days" cop={totals.heating?.cop} />
            <CopCard title="Hot water COP, all days" cop={totals.hotWater?.cop} />
            <CopCard title="SCOP" cop={totals.overall?.cop} />
          </div>
          <p className="text-muted small" style={{ marginBottom: "20px" }}>
            {totals.days} {totals.days === 1 ? "day" : "days"} recorded since {totals.from}.
          </p>
        </>
      )}

      {!loading && !error && hasPoints && (
        <>
          {/* Heat delivered in each mode, and all the electricity the day used. Heat metered while
              neither mode was on is in neither card. */}
          <div className="card-group" style={{ marginBottom: "20px" }}>
            <EnergyCard title="Heating" kwh={modeHeatKwh(data?.modes?.heating)} />
            <EnergyCard title="Hot water" kwh={modeHeatKwh(data?.modes?.hotWater)} />
            <EnergyCard title="Electricity in" kwh={elecKwh} />
          </div>

          {/* The day's energy out over energy in: for the slots recorded as heating, for those
              recorded as hot water, and for every slot. Standby and anything else in neither mode
              counts toward the overall figure only. */}
          <div className="card-group" style={{ marginBottom: "20px" }}>
            <CopCard title="Heating COP" cop={data?.modes?.heating?.cop} />
            <CopCard title="Hot water COP" cop={data?.modes?.hotWater?.cop} />
            <CopCard title="Overall COP" cop={data?.cop} />
          </div>

          <Chart title="Power" data={powerData} series={POWER_SERIES} unit="W" modes={modes} />
          <Chart title="Temperatures" data={tempData} series={TEMP_SERIES} unit="&deg;C" modes={modes} />
          {/* Instantaneous COP, unlike the card above, which is the day's integrated figure.
              Gaps are the heat source being off rather than missing data. */}
          <Chart title="COP" data={copData} series={COP_SERIES} unit="" decimals={2} modes={modes} />
        </>
      )}
    </>
  );
}

export default History;
