import { useEffect, useMemo, useState } from "react";
import { DateToolbar } from "./HistoryChart";
import { shiftDate, todayISO } from "./historyData";
import PageHeader from "./PageHeader";

// When each device was and was not subscribed over a day, from GET /api/history/subscriptions.
// The firmware records every subscription established, ended and failed -- see
// firmware/main/storage/subscription_log.h -- and this replays those events into a timeline per
// device.

type SubEvent = {
  ts: number;
  event: "boot" | "dayStart" | "upAtStart" | "established" | "terminated" | "failed" | "removed";
  nodeId?: number;
  // boot only: the monitor's last sign of life before it, null when that was before this day.
  lastAlive?: number | null;
  // failed only, when the stack gave a reason.
  code?: number;
  error?: string;
};

type SubLogResponse = {
  storage: string;
  date?: string;
  now?: number;
  events?: SubEvent[];
  lastAlive?: number | null;
};

type ApiNode = { nodeId: number; nodeName?: string; productName?: string; hasSubscription?: boolean };

type SegmentState = "up" | "down" | "off";
type Segment = { from: number; to: number; state: SegmentState };
type Failure = { ts: number; error?: string };

type Outage = { from: number; to: number; failures: Failure[] };

type NodeTimeline = {
  nodeId: number;
  segments: Segment[];
  outages: Outage[];
  drops: number;
  failures: number;
  upSeconds: number;
  downSeconds: number;
};

const STATE_COLOURS: Record<SegmentState, string> = { up: "#198754", down: "#dc3545", off: "#adb5bd" };
const STATE_LABELS: Record<SegmentState, string> = {
  up: "Subscribed",
  down: "Not subscribed",
  off: "Monitor off",
};

// The monitor writes a sign of life every minute. Silence for longer than this at the end of the
// record means it is off, rather than between writes.
const ALIVE_GRACE_S = 180;

const BAR_HEIGHT_PX = 16;

function midnightOf(date: string): number {
  return new Date(`${date}T00:00:00`).getTime() / 1000;
}

function formatTime(ts: number): string {
  return new Date(ts * 1000).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
}

function formatDuration(seconds: number): string {
  const s = Math.round(seconds);
  if (s < 60) {
    return `${s}s`;
  }
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  return h > 0 ? `${h}h ${m}m` : `${m}m`;
}

// Replays the day's events into one timeline per node.
//
// A day file opens with dayStart -- every node unsubscribed -- then upAtStart for each that was, so
// the state at midnight is known. A boot unsubscribes everything; the time between the monitor's
// last sign of life and the boot is drawn as "off" for every node, since nothing was recorded. The
// same goes for the end of a past day the monitor did not see out.
function buildTimelines(log: SubLogResponse, date: string, nodeIds: number[]): NodeTimeline[] {
  const events = log.events ?? [];
  const dayStart = midnightOf(date);
  const dayEnd = midnightOf(shiftDate(date, 1));
  const isToday = date === todayISO();
  const end = isToday ? Math.min(dayEnd, log.now ?? Date.now() / 1000) : dayEnd;
  const clamp = (ts: number) => Math.min(end, Math.max(dayStart, ts));

  const ids = new Set(nodeIds);
  for (const e of events) {
    if (e.nodeId !== undefined) {
      ids.add(e.nodeId);
    }
  }

  type Running = NodeTimeline & { state: SegmentState | null; since: number; outage: Outage | null };
  const nodes = new Map<number, Running>();
  for (const id of ids) {
    nodes.set(id, {
      nodeId: id,
      segments: [],
      outages: [],
      drops: 0,
      failures: 0,
      upSeconds: 0,
      downSeconds: 0,
      state: null,
      since: dayStart,
      outage: null,
    });
  }

  // Closes the node's current stretch at `at` and starts `state` from there. A change to the
  // state it is already in is not a change.
  const setState = (n: Running, state: SegmentState | null, at: number) => {
    const ts = clamp(at);
    if (state === n.state) {
      return;
    }
    if (n.state !== null && ts > n.since) {
      n.segments.push({ from: n.since, to: ts, state: n.state });
      if (n.state === "up") {
        n.upSeconds += ts - n.since;
      } else if (n.state === "down") {
        n.downSeconds += ts - n.since;
      }
    }
    if (n.outage) {
      n.outage.to = ts;
      if (n.outage.to > n.outage.from) {
        n.outages.push(n.outage);
      }
      n.outage = null;
    }
    if (state === "down") {
      n.outage = { from: ts, to: ts, failures: [] };
    }
    n.state = state;
    n.since = ts;
  };

  for (const e of events) {
    const n = e.nodeId !== undefined ? nodes.get(e.nodeId) : undefined;

    switch (e.event) {
      case "dayStart":
        nodes.forEach((each) => setState(each, "down", e.ts));
        break;
      case "upAtStart":
      case "established":
        if (n) setState(n, "up", e.ts);
        break;
      case "terminated":
        if (n) {
          if (n.state === "up") n.drops++;
          setState(n, "down", e.ts);
        }
        break;
      case "failed":
        if (n) {
          setState(n, "down", e.ts);
          n.failures++;
          n.outage?.failures.push({ ts: e.ts, error: e.error });
        }
        break;
      case "removed":
        if (n) setState(n, null, e.ts);
        break;
      case "boot":
        nodes.forEach((each) => {
          setState(each, "off", e.lastAlive ?? dayStart);
          setState(each, "down", e.ts);
        });
        break;
    }
  }

  // The monitor stopped writing before the end of the record: it was off from its last write.
  const lastAlive = log.lastAlive;
  if (lastAlive != null && end - lastAlive > ALIVE_GRACE_S) {
    nodes.forEach((each) => {
      if (each.state !== null) setState(each, "off", lastAlive);
    });
  }

  nodes.forEach((each) => setState(each, null, end));
  return [...nodes.values()];
}

function TimelineBar({ timeline, from, to }: { timeline: NodeTimeline; from: number; to: number }) {
  const span = to - from;
  return (
    <div
      style={{
        position: "relative",
        height: `${BAR_HEIGHT_PX}px`,
        background: "var(--bs-tertiary-bg, #f1f3f5)",
        borderRadius: "3px",
        overflow: "hidden",
      }}
    >
      {timeline.segments.map((s, i) => (
        <div
          key={i}
          title={`${STATE_LABELS[s.state]} ${formatTime(s.from)}–${formatTime(s.to)} (${formatDuration(s.to - s.from)})`}
          style={{
            position: "absolute",
            top: 0,
            bottom: 0,
            left: `${((s.from - from) / span) * 100}%`,
            width: `${((s.to - s.from) / span) * 100}%`,
            minWidth: "1px",
            background: STATE_COLOURS[s.state],
          }}
        />
      ))}
    </div>
  );
}

// Hour labels under the bars, every three hours.
function HourScale({ from, to }: { from: number; to: number }) {
  const span = to - from;
  const marks = [];
  for (let h = 0; h <= 24; h += 3) {
    const ts = from + h * 3600;
    if (ts > to) break;
    marks.push(
      <span
        key={h}
        className="text-muted small"
        style={{
          position: "absolute",
          left: `${((ts - from) / span) * 100}%`,
          transform: h === 0 ? "none" : h === 24 ? "translateX(-100%)" : "translateX(-50%)",
        }}
      >
        {String(h).padStart(2, "0")}:00
      </span>
    );
  }
  return <div style={{ position: "relative", height: "1.5em" }}>{marks}</div>;
}

// The reasons attempts failed during an outage, most frequent first.
function failureSummary(failures: Failure[]): string {
  if (failures.length === 0) {
    return "";
  }
  const counts = new Map<string, number>();
  for (const f of failures) {
    const reason = f.error ?? "no reason given";
    counts.set(reason, (counts.get(reason) ?? 0) + 1);
  }
  return [...counts.entries()]
    .sort((a, b) => b[1] - a[1])
    .map(([reason, count]) => `${count}× ${reason}`)
    .join("; ");
}

function NodeCard({ timeline, name, from, to }: { timeline: NodeTimeline; name: string; from: number; to: number }) {
  const recorded = timeline.upSeconds + timeline.downSeconds;
  const connected = recorded > 0 ? (timeline.upSeconds / recorded) * 100 : null;
  const longest = timeline.outages.reduce((max, o) => Math.max(max, o.to - o.from), 0);

  return (
    <div className="card" style={{ marginBottom: "12px" }}>
      <div className="card-body" style={{ padding: "12px 16px" }}>
        <div style={{ display: "flex", flexWrap: "wrap", justifyContent: "space-between", gap: "4px 16px" }}>
          <strong>
            {name} <span className="text-muted small">0x{timeline.nodeId.toString(16).toUpperCase()}</span>
          </strong>
          <span className="small">
            {connected === null ? "–" : `${connected.toFixed(1)}% subscribed`}
            {" · "}
            {timeline.drops} {timeline.drops === 1 ? "drop" : "drops"}
            {" · "}
            {timeline.failures} failed {timeline.failures === 1 ? "attempt" : "attempts"}
            {longest > 0 && <> {"·"} longest outage {formatDuration(longest)}</>}
          </span>
        </div>
        <div style={{ marginTop: "8px" }}>
          <TimelineBar timeline={timeline} from={from} to={to} />
        </div>
        {timeline.outages.length > 0 && (
          <details style={{ marginTop: "8px" }}>
            <summary className="small">
              {timeline.outages.length} {timeline.outages.length === 1 ? "outage" : "outages"}
            </summary>
            <div className="table-responsive">
              <table className="table table-sm small" style={{ marginTop: "8px", marginBottom: 0 }}>
                <thead>
                  <tr>
                    <th>From</th>
                    <th>To</th>
                    <th>Length</th>
                    <th>Failed attempts</th>
                  </tr>
                </thead>
                <tbody>
                  {timeline.outages.map((o, i) => (
                    <tr key={i}>
                      <td>{formatTime(o.from)}</td>
                      <td>{formatTime(o.to)}</td>
                      <td>{formatDuration(o.to - o.from)}</td>
                      <td>{failureSummary(o.failures) || "–"}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </details>
        )}
      </div>
    </div>
  );
}

function Diagnostics() {
  const [date, setDate] = useState(todayISO());
  const [log, setLog] = useState<SubLogResponse | null>(null);
  const [nodes, setNodes] = useState<ApiNode[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;

    const load = async () => {
      try {
        const [logResponse, nodesResponse] = await Promise.all([
          fetch(`/api/history/subscriptions?date=${date}`),
          fetch("/api/nodes"),
        ]);
        if (!logResponse.ok) {
          throw new Error(`The device answered ${logResponse.status}`);
        }
        const body: SubLogResponse = await logResponse.json();
        const nodeList: ApiNode[] = nodesResponse.ok ? await nodesResponse.json() : [];
        if (!cancelled) {
          setLog(body);
          setNodes(nodeList);
          setError(null);
        }
      } catch (e) {
        if (!cancelled) {
          setError(`Could not load the subscription log: ${e instanceof Error ? e.message : e}`);
        }
      } finally {
        if (!cancelled) {
          setLoading(false);
        }
      }
    };

    setLoading(true);
    load();

    // Today keeps moving, so it is refreshed; the device writes once a minute.
    const timer = date === todayISO() ? setInterval(load, 60000) : undefined;

    return () => {
      cancelled = true;
      if (timer) clearInterval(timer);
    };
  }, [date]);

  const timelines = useMemo(() => {
    if (!log || log.storage !== "ok" || (log.events ?? []).length === 0) {
      return [];
    }
    const built = buildTimelines(
      log,
      date,
      nodes.map((n) => n.nodeId)
    ).filter((t) => t.segments.length > 0);

    // Worst first: the device that is struggling is the one being looked for.
    return built.sort((a, b) => b.downSeconds - a.downSeconds || a.nodeId - b.nodeId);
  }, [log, nodes, date]);

  const names = new Map(nodes.map((n) => [n.nodeId, n.nodeName || n.productName || ""]));
  const from = midnightOf(date);
  const to = midnightOf(shiftDate(date, 1));

  return (
    <>
      <PageHeader title="Diagnostics" />

      <p className="text-muted">
        When each device held a subscription. A device that is not subscribed sends no readings, so
        its values are missing from history for that time.
      </p>

      <DateToolbar date={date} setDate={setDate} />

      {loading && !log && <p>Loading&hellip;</p>}

      {error && <div className="alert alert-danger">{error}</div>}

      {!error && log?.storage === "unavailable" && (
        <div className="alert alert-warning">
          No SD card is fitted, so subscriptions are not being recorded.
        </div>
      )}

      {!error && log?.storage === "ok" && timelines.length === 0 && (
        <div className="alert alert-info">No subscriptions recorded on {date}.</div>
      )}

      {timelines.length > 0 && (
        <>
          <div className="small" style={{ marginBottom: "12px" }}>
            {(["up", "down", "off"] as const).map((state) => (
              <span key={state} style={{ marginRight: "16px", whiteSpace: "nowrap" }}>
                <span
                  style={{
                    display: "inline-block",
                    width: "12px",
                    height: "8px",
                    marginRight: "4px",
                    background: STATE_COLOURS[state],
                  }}
                />
                {STATE_LABELS[state]}
              </span>
            ))}
          </div>

          {timelines.map((t) => (
            <NodeCard
              key={t.nodeId}
              timeline={t}
              name={names.get(t.nodeId) || "Removed device"}
              from={from}
              to={to}
            />
          ))}

          <HourScale from={from} to={to} />
        </>
      )}
    </>
  );
}

export default Diagnostics;
