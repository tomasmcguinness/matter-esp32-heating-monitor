import { useState } from "react";
import { ComfortLegend, ComfortScopeToggle, RoomComfortDays } from "./Comfort";
import { COMFORT_SUMMARY_DAYS, describe, formatOffset, latestDate, recentRecords, useComfort } from "./comfortData";
import { summariseComfort, type ComfortScope } from "./historyData";

// A room's time at target over the last four weeks, day by day, on the room's page.
function RoomComfort({ roomId }: { roomId: number }) {
  const [scope, setScope] = useState<ComfortScope>("all");
  const { body, error } = useComfort();

  const records = body?.records ?? [];
  const today = latestDate(records);
  const mine = today ? recentRecords(records, roomId, today, 28) : [];
  const week = summariseComfort(today ? recentRecords(records, roomId, today, COMFORT_SUMMARY_DAYS) : [], scope);
  const band = body?.bandC100 ?? 20;

  return (
    <div className="card" style={{ marginBottom: "20px" }}>
      <div className="card-header d-flex justify-content-between align-items-center flex-wrap gap-2">
        <span>Time at target, last 4 weeks</span>
        <ComfortScopeToggle scope={scope} setScope={setScope} />
      </div>
      <div className="card-body">
        {error && <span className="text-muted">{error}</span>}
        {!error && !body && <span className="text-muted">Loading&hellip;</span>}
        {body && body.storage !== "ok" && <span className="text-muted">No SD card is fitted, so nothing is being recorded.</span>}
        {body?.storage === "ok" && (mine.length === 0 || !today) && <span className="text-muted">Nothing recorded for this room yet.</span>}
        {body?.storage === "ok" && today && mine.length > 0 && (
          <>
            <p className="mb-2">
              Last 7 days: <strong>{formatOffset(week.meanC100)}</strong> on average
              {week.total > 0 && <span className="text-muted">, {describe(week)}</span>}
            </p>
            <RoomComfortDays records={mine} today={today} scope={scope} />
            <div className="mt-2"><ComfortLegend band={band} /></div>
          </>
        )}
      </div>
    </div>
  );
}

export default RoomComfort;
