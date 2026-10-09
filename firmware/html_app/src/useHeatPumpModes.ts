import { useEffect, useState } from "react";
import { modeSpans, type HistoryResponse, type ModeSpan } from "./historyData";

// One shared empty list, because Chart compares its modes prop by identity.
const NO_MODES: ModeSpan[] = [];

// The heat pump's heating and hot water periods for a day, for charts that are not of the home
// series themselves -- a room's or a radiator's. The spans are in absolute time, so they need no
// joining against the chart's own points.
//
// With no `date` the request carries none, which means the device's own local day -- see
// RoomTodayChart for why that beats sending the browser's date.
//
// 800 points even under a 200-point today chart: a bucket only counts as on when the mode held for
// all of it, so coarser buckets would trim more off each end of a run.
//
// Any failure is an empty list. The bar is an annotation, and a chart without it is still right.
export function useHeatPumpModes(date: string | undefined): ModeSpan[] {
  const [modes, setModes] = useState<ModeSpan[]>(NO_MODES);

  useEffect(() => {
    let cancelled = false;

    const load = async () => {
      let loaded = NO_MODES;

      try {
        const dateParam = date === undefined ? "" : `&date=${date}`;
        const response = await fetch(`/api/history?points=800${dateParam}`);

        if (response.ok) {
          const body: HistoryResponse = await response.json();
          const spans = modeSpans(body);

          if (spans.length > 0) {
            loaded = spans;
          }
        }
      } catch {
        /* the bar is simply absent */
      }

      if (!cancelled) {
        setModes(loaded);
      }
    };

    load();

    return () => {
      cancelled = true;
    };
  }, [date]);

  return modes;
}
