import { useEffect, useState } from "react";
import { isoDaysBefore, type ComfortRecord, type ComfortResponse, type ComfortSummary } from "./historyData";

// The data side of Comfort.tsx, apart from its components so that React Fast Refresh keeps working.

export const COMFORT_UNDER = "#2b6cb0";
export const COMFORT_ON = "#2f855a";
export const COMFORT_OVER = "#c0392b";
export const NEUTRAL = "#e9ecef";

// How many days the grid and the room chart cover; the table's columns use the last 7 of them.
export const COMFORT_DAYS = 28;
export const COMFORT_SUMMARY_DAYS = 7;

export function useComfort(days: number = COMFORT_DAYS) {
  const [body, setBody] = useState<ComfortResponse | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;

    fetch(`/api/history/comfort?days=${days}`)
      .then(async (r) => {
        if (!r.ok) {
          throw new Error(`Request failed: ${r.status}`);
        }
        return (await r.json()) as ComfortResponse;
      })
      .then((b) => !cancelled && setBody(b))
      .catch((e) => !cancelled && setError(String(e)));

    return () => {
      cancelled = true;
    };
  }, [days]);

  return { body, error };
}

// The last day the device sent anything for -- its today -- so the windows below follow the
// board's clock rather than the browser's.
export function latestDate(records: ComfortRecord[]): string | null {
  return records.reduce<string | null>((m, r) => (m === null || r.date > m ? r.date : m), null);
}

// One room's records over the last `days` days up to `today`.
export function recentRecords(records: ComfortRecord[], roomId: number, today: string, days: number) {
  const from = isoDaysBefore(today, days - 1);
  return records.filter((r) => r.roomId === roomId && r.date >= from && r.date <= today);
}

export function formatOffset(meanC100: number | null) {
  if (meanC100 === null) {
    return "-";
  }
  const v = meanC100 / 100;
  return `${v > 0 ? "+" : v < 0 ? "−" : "±"}${Math.abs(v).toFixed(1)} °C`;
}

const pct = (part: number, total: number) => (total > 0 ? Math.round((part / total) * 100) : 0);

export function describe(summary: ComfortSummary) {
  const { under, on, over, total } = summary;
  return `${pct(under, total)}% under, ${pct(on, total)}% on target, ${pct(over, total)}% over` +
    (summary.estimated ? " (partly judged against today's target)" : "");
}

