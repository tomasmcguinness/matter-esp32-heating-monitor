import { useEffect, useMemo, useState } from "react";
import type uPlot from "uplot";
import { Chart, ChartCard } from "./HistoryChart";

// The heat balance (TASK-45): for each 10 minutes of local clock time, the heat meter's average
// output while space heating against the live model's -- radiators by their dT50 rating plus UFH by
// calibrated loop flow -- and the gap between them. "Hourly" adds the buckets up by clock hour.
//
// GET /api/history/balance records what the model said at the time, with the calibration points
// and radiator ratings then in force; it is never recalculated. Only space heating counts (not hot
// water, not defrosting), and only while all three figures were known: `countedSeconds`.
//
type BalanceBucket = {
  start: number;
  countedSeconds: number;
  unknownSeconds: number;
  heatMeterWh: number;
  radiatorsWh: number;
  ufhWh: number;
};

type BalanceResponse = {
  storage: "ok" | "unavailable";
  date?: string;
  bucketSeconds?: number;
  buckets?: BalanceBucket[];
  current?: BalanceBucket | null;
};

type View = "buckets" | "hourly";

// A bucket with less space heating than this is too little to compare, and is left as a gap: 3
// minutes of a 10-minute bucket, 10 of an hour.
const minCountedS = (bucketS: number) => Math.max(180, bucketS / 6);

// Module-level, because Chart compares its series prop by identity.
const BALANCE_SERIES = [
  { label: "Heat meter", stroke: "#d9534f" },
  { label: "Radiators + UFH", stroke: "#0275d8" },
];

const GAP_SERIES = [
  { label: "Gap", stroke: "#9354d9" },
  { label: "Gap %", stroke: "#f0ad4e", axis: "right" as const },
];

// The start of the local clock hour containing a Unix time.
function localHourStart(ts: number): number {
  const d = new Date(ts * 1000);
  d.setMinutes(0, 0, 0);
  return Math.floor(d.getTime() / 1000);
}

// Buckets added up by local clock hour.
function byHour(buckets: BalanceBucket[]): BalanceBucket[] {
  const hours = new Map<number, BalanceBucket>();

  for (const b of buckets) {
    const start = localHourStart(b.start);
    const h = hours.get(start) ?? { start, countedSeconds: 0, unknownSeconds: 0, heatMeterWh: 0, radiatorsWh: 0, ufhWh: 0 };
    h.countedSeconds += b.countedSeconds;
    h.unknownSeconds += b.unknownSeconds;
    h.heatMeterWh += b.heatMeterWh;
    h.radiatorsWh += b.radiatorsWh;
    h.ufhWh += b.ufhWh;
    hours.set(start, h);
  }

  return [...hours.values()].sort((a, b) => a.start - b.start);
}

function HeatBalance({ date }: { date: string }) {
  const [body, setBody] = useState<BalanceResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [view, setView] = useState<View>("buckets");

  // History renders this keyed by date, so a new day is a fresh component with fresh state.
  useEffect(() => {
    let cancelled = false;

    fetch(`/api/history/balance?date=${date}`)
      .then(async (r) => {
        if (!r.ok) {
          throw new Error(`Request failed: ${r.status}`);
        }
        return (await r.json()) as BalanceResponse;
      })
      .then((b) => !cancelled && setBody(b))
      .catch((e) => !cancelled && setError(String(e)));

    return () => {
      cancelled = true;
    };
  }, [date]);

  const fileBucketS = body?.bucketSeconds ?? 600;

  const { balanceData, gapData, dayGapPct, countedHours, bucketCount } = useMemo(() => {
    // The day's buckets, plus the one in progress today.
    const raw: BalanceBucket[] =
      body && body.storage === "ok" ? [...(body.buckets ?? []), ...(body.current ? [body.current] : [])] : [];
    const hourly = view === "hourly" && fileBucketS < 3600;
    const buckets = hourly ? byHour(raw) : raw;
    const bucketS = hourly ? 3600 : fileBucketS;
    const minS = minCountedS(bucketS);

    const xs: number[] = [];
    const meter: (number | null)[] = [];
    const model: (number | null)[] = [];
    const gapW: (number | null)[] = [];
    const gapPct: (number | null)[] = [];
    let meterWh = 0;
    let modelWh = 0;
    let counted = 0;

    for (const b of buckets) {
      // Plotted at the middle of the time it covers.
      xs.push(b.start + bucketS / 2);

      if (b.countedSeconds < minS) {
        meter.push(null);
        model.push(null);
        gapW.push(null);
        gapPct.push(null);
        continue;
      }

      // Average watts over the counted seconds.
      const m = (b.heatMeterWh * 3600) / b.countedSeconds;
      const e = ((b.radiatorsWh + b.ufhWh) * 3600) / b.countedSeconds;
      meter.push(Math.round(m));
      model.push(Math.round(e));
      gapW.push(Math.round(m - e));
      gapPct.push(m === 0 ? null : ((m - e) / m) * 100);

      meterWh += b.heatMeterWh;
      modelWh += b.radiatorsWh + b.ufhWh;
      counted += b.countedSeconds;
    }

    return {
      balanceData: xs.length ? ([xs, meter, model] as unknown as uPlot.AlignedData) : null,
      gapData: xs.length ? ([xs, gapW, gapPct] as unknown as uPlot.AlignedData) : null,
      dayGapPct: meterWh !== 0 ? ((meterWh - modelWh) / meterWh) * 100 : null,
      countedHours: counted / 3600,
      bucketCount: raw.length,
    };
  }, [body, view, fileBucketS]);

  if (error) {
    return <ChartCard title="Heat balance">{error}</ChartCard>;
  }

  if (!body) {
    return <ChartCard title="Heat balance"><span className="text-muted">Loading&hellip;</span></ChartCard>;
  }

  if (body.storage !== "ok" || bucketCount === 0) {
    return (
      <ChartCard title="Heat balance">
        <span className="text-muted">No heat balance recorded on {date}. It is recorded from 24 November 2026 on.</span>
      </ChartCard>
    );
  }

  const bucketLabel = fileBucketS >= 3600 ? "Hourly" : `${fileBucketS / 60} min`;

  return (
    <>
      <div className="d-flex justify-content-end mb-2">
        <div className="btn-group btn-group-sm" role="group" aria-label="Heat balance resolution">
          <button type="button" className={`btn btn-outline-secondary${view === "buckets" ? " active" : ""}`} onClick={() => setView("buckets")}>
            {bucketLabel}
          </button>
          {fileBucketS < 3600 &&
            <button type="button" className={`btn btn-outline-secondary${view === "hourly" ? " active" : ""}`} onClick={() => setView("hourly")}>
              Hourly
            </button>}
        </div>
      </div>
      <Chart title="Heat balance (average while space heating)" data={balanceData} series={BALANCE_SERIES} unit="W" decimals={0} />
      <Chart title="Gap: heat meter − (radiators + UFH)" data={gapData} series={GAP_SERIES} unit="W" decimals={0} rightUnit="%" rightDecimals={1} />
      <p className="text-muted small" style={{ marginBottom: "20px" }}>
        {dayGapPct === null
          ? "No period had enough space heating to compare."
          : <>Over {countedHours.toFixed(1)} h of space heating the heat meter was {Math.abs(dayGapPct).toFixed(1)}% {dayGapPct >= 0 ? "above" : "below"} radiators + UFH. </>}
        Recorded live, with the calibration points and radiator ratings in force at the time, and not recalculated. Only space
        heating counts: hot water and defrosts are left out, and a period with too little of it is left blank.
      </p>
    </>
  );
}

export default HeatBalance;
