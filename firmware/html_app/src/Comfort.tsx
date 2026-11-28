import {
  isoDaysBefore,
  summariseComfort,
  type ComfortRecord,
  type ComfortScope,
  type ComfortSummary,
} from "./historyData";
import { COMFORT_DAYS, COMFORT_ON, COMFORT_OVER, COMFORT_UNDER, NEUTRAL, describe, formatOffset } from "./comfortData";

// Time at target: how long each room spent under, on and over its target, from
// GET /api/history/comfort. The Rooms page shows a bar and an average per room and a grid of
// rooms by day; a room's page shows its own days. The colours match the Heat tab's markers.

// A thin bar split into time under, on and over target.
export function ComfortBar({ summary, width = 120 }: { summary: ComfortSummary; width?: number }) {
  if (summary.total === 0) {
    return <span className="text-muted">-</span>;
  }

  const seg = (value: number, color: string) =>
    value > 0 ? <div style={{ width: `${(value / summary.total) * 100}%`, background: color }} /> : null;

  return (
    <div
      title={describe(summary)}
      style={{ display: "flex", width: `${width}px`, height: "10px", borderRadius: "2px", overflow: "hidden", background: NEUTRAL }}
    >
      {seg(summary.under, COMFORT_UNDER)}
      {seg(summary.on, COMFORT_ON)}
      {seg(summary.over, COMFORT_OVER)}
    </div>
  );
}

export function ComfortScopeToggle({ scope, setScope }: { scope: ComfortScope; setScope: (s: ComfortScope) => void }) {
  return (
    <div className="btn-group btn-group-sm" role="group" aria-label="Which time counts">
      <button type="button" className={`btn btn-outline-secondary${scope === "all" ? " active" : ""}`} onClick={() => setScope("all")}>
        All hours
      </button>
      <button
        type="button"
        className={`btn btn-outline-secondary${scope === "heating" ? " active" : ""}`}
        onClick={() => setScope("heating")}
        title="Only the time the heat pump was space heating"
      >
        While heating
      </button>
    </div>
  );
}

export function ComfortLegend({ band }: { band: number }) {
  const key = (color: string, label: string) => (
    <span style={{ marginRight: "12px", whiteSpace: "nowrap" }}>
      <span style={{ display: "inline-block", width: "12px", height: "10px", background: color, marginRight: "4px" }} />
      {label}
    </span>
  );

  return (
    <span className="text-muted small">
      {key(COMFORT_UNDER, "under")}
      {key(COMFORT_ON, `within ${(band / 100).toFixed(1)} °C`)}
      {key(COMFORT_OVER, "over")}
    </span>
  );
}

// A day's mean distance from target as a colour: blue below, red above, fading to grey within the
// band, at full strength from 2 °C out.
function offsetColor(meanC100: number | null, band: number) {
  if (meanC100 === null) {
    return null;
  }
  if (Math.abs(meanC100) <= band) {
    return NEUTRAL;
  }

  const t = Math.min((Math.abs(meanC100) - band) / (200 - band), 1);
  const to = meanC100 > 0 ? [0xc0, 0x39, 0x2b] : [0x2b, 0x6c, 0xb0];
  const from = [0xe9, 0xec, 0xef];
  const mix = from.map((f, i) => Math.round(f + (to[i] - f) * (0.25 + 0.75 * t)));
  return `rgb(${mix.join(",")})`;
}

const shortDate = (iso: string) =>
  new Date(`${iso}T12:00:00`).toLocaleDateString(undefined, { day: "numeric", month: "short" });

// Rooms down, days across, each square the room's mean distance from its target that day. A room
// red all the way across is always warm; one that turns blue on the cold days is short of heat.
export function ComfortGrid({
  rooms,
  records,
  today,
  scope,
  band,
}: {
  rooms: { roomId: number; name: string }[];
  records: ComfortRecord[];
  today: string;
  scope: ComfortScope;
  band: number;
}) {
  const dates = Array.from({ length: COMFORT_DAYS }, (_, i) => isoDaysBefore(today, COMFORT_DAYS - 1 - i));
  const byKey = new Map(records.map((r) => [`${r.roomId}/${r.date}`, r]));

  return (
    <div style={{ overflowX: "auto" }}>
      <table style={{ borderCollapse: "separate", borderSpacing: "2px", width: "100%", tableLayout: "fixed" }}>
        <colgroup>
          <col style={{ width: "130px" }} />
          {dates.map((d) => <col key={d} />)}
        </colgroup>
        <thead>
          <tr>
            <th />
            {dates.map((d, i) => (
              <th key={d} className="text-muted small fw-normal" style={{ textAlign: "left", whiteSpace: "nowrap", overflow: "visible" }}>
                {i % 7 === 0 ? shortDate(d) : ""}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {rooms.map((room) => (
            <tr key={room.roomId}>
              <td className="small" style={{ whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis", paddingRight: "6px" }}>{room.name}</td>
              {dates.map((d) => {
                const rec = byKey.get(`${room.roomId}/${d}`);
                const summary = rec ? summariseComfort([rec], scope) : null;
                const color = summary ? offsetColor(summary.meanC100, band) : null;
                const title = summary && summary.total > 0
                  ? `${room.name}, ${shortDate(d)}${d === today ? " (so far)" : ""}: ${formatOffset(summary.meanC100)} on average; ${describe(summary)}`
                  : `${room.name}, ${shortDate(d)}: nothing recorded`;

                return (
                  <td
                    key={d}
                    title={title}
                    style={{
                      height: "18px",
                      borderRadius: "2px",
                      background: color ?? "transparent",
                      border: color ? "none" : `1px dashed ${NEUTRAL}`,
                      opacity: d === today ? 0.6 : 1,
                    }}
                  />
                );
              })}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

// One room's days as stacked columns: over on top, on target, under at the bottom, each day's
// column the full height of the time that counted.
export function RoomComfortDays({
  records,
  today,
  scope,
}: {
  records: ComfortRecord[];
  today: string;
  scope: ComfortScope;
}) {
  const dates = Array.from({ length: COMFORT_DAYS }, (_, i) => isoDaysBefore(today, COMFORT_DAYS - 1 - i));
  const byDate = new Map(records.map((r) => [r.date, r]));
  const HEIGHT = 90;

  return (
    <div>
      <div style={{ display: "flex", alignItems: "flex-end", gap: "2px", height: `${HEIGHT}px` }}>
        {dates.map((d) => {
          const rec = byDate.get(d);
          const s = rec ? summariseComfort([rec], scope) : null;

          if (!s || s.total === 0) {
            return <div key={d} title={`${shortDate(d)}: nothing recorded`} style={{ flex: 1, height: "100%", border: `1px dashed ${NEUTRAL}`, borderRadius: "2px" }} />;
          }

          const seg = (v: number, color: string) => (v > 0 ? <div style={{ height: `${(v / s.total) * 100}%`, background: color }} /> : null);

          return (
            <div
              key={d}
              title={`${shortDate(d)}${d === today ? " (so far)" : ""}: ${formatOffset(s.meanC100)} on average; ${describe(s)}`}
              style={{ flex: 1, height: "100%", display: "flex", flexDirection: "column", borderRadius: "2px", overflow: "hidden", opacity: d === today ? 0.6 : 1 }}
            >
              {seg(s.over, COMFORT_OVER)}
              {seg(s.on, COMFORT_ON)}
              {seg(s.under, COMFORT_UNDER)}
            </div>
          );
        })}
      </div>
      <div style={{ display: "flex", gap: "2px" }}>
        {dates.map((d, i) => (
          <div key={d} className="text-muted small" style={{ flex: 1, whiteSpace: "nowrap", overflow: "visible" }}>
            {i % 7 === 0 ? shortDate(d) : ""}
          </div>
        ))}
      </div>
    </div>
  );
}
