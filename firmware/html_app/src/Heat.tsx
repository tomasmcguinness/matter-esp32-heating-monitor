import { useCallback, useContext, useEffect, useState, type ReactNode } from "react";
import { WebSocketContext } from "./WSContext.jsx";
import { useHome } from "./HomeContext";
import PageHeader from "./PageHeader";
import HeatLoss from "./HeatLoss";
import { layoutSankey, type SankeyLink, type SankeyNode } from "./sankey";
import "./Heat.css";

// Where the metered heat goes right now: the heat meter on the left, split into what the radiators
// and the UFH manifolds are calculated to be giving out, and from those into each room. The middle
// and right columns are the live model (radiator dT50 ratings, calibrated UFH flow), so the gap
// between them and the meter is the same question the heat balance on the History page answers
// over time.
//
// Everything comes from GET /api/rooms, which carries each room's radiators and UFH shares inline,
// and the "home" payload. Radiator outputs follow the "radiator" push. A room's UFH input is not
// pushed; the firmware recalculates it at most every 30 s, so the rooms are re-fetched on that
// cadence.

type Radiator = {
  radiatorId: number;
  name: string;
  currentOutput: number | null;
};

type Room = {
  roomId: number;
  name: string;
  radiators: Radiator[];
  ufhInput: number | null;
  currentTemperature: number | null;
  measuredHeatLoss: number | null;
  ufhShares: { emitterId: number; pct: number }[];
};

type RadiatorPush = {
  radiatorId: number;
  currentOutput?: number | null;
};

type HeatNode = SankeyNode & {
  label: string;
  color: string;
  style?: "hatched" | "dashed";
};

type HeatLink = SankeyLink & {
  color: string;
};

const ROOMS_REFRESH_MS = 30000;

const COLOR_METER = "#c94f24";
const COLOR_RADIATORS = "#e0a030";
const COLOR_UFH = "#8a5cc2";
const COLOR_ROOM = "#2a8f86";
const COLOR_GREY = "#9a9a9a";

const WIDTH = 900;
const NODE_WIDTH = 14;
const MARGIN_LEFT = 120;
const MARGIN_RIGHT = 190;
// Room above the top node and below the bottom one for the two lines of their labels.
const LABEL_PAD = 20;

function watts(value: number) {
  const w = Math.round(value);
  return Math.abs(w) >= 1000 ? `${(w / 1000).toFixed(1)} kW` : `${w} W`;
}

function radiatorSum(room: Room) {
  return room.radiators.reduce((s, r) => s + (r.currentOutput ?? 0), 0);
}

// The diagram, or the reason there isn't one, in a card like the heat loss figures above it.
function FlowCard({ children }: { children: ReactNode }) {
  return (
    <div className="card">
      <div className="card-header">Where the heat goes</div>
      <div className="card-body">{children}</div>
    </div>
  );
}

function Heat() {
  const [rooms, setRooms] = useState<Room[] | null>(null);
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const home: any = useHome();

  const { subscribe, unsubscribe } = useContext(WebSocketContext);

  useEffect(() => {
    let cancelled = false;

    const fetchRooms = async () => {
      try {
        const response = await fetch("/api/rooms");

        if (response.ok && !cancelled) {
          const data: Room[] = await response.json();
          setRooms(data.sort((a, b) => a.name.localeCompare(b.name)));
        }
      } catch {
        // Keep what we have; the next refresh will try again.
      }
    };

    fetchRooms();
    const timer = setInterval(fetchRooms, ROOMS_REFRESH_MS);

    return () => {
      cancelled = true;
      clearInterval(timer);
    };
  }, []);

  // Patched inside the updater, as Layout does, so the handler registered at mount never works
  // against a stale list.
  //
  const applyRadiator = useCallback((message: RadiatorPush) => {
    if (message.currentOutput === undefined) {
      return;
    }

    setRooms((previous) => previous === null ? previous : previous.map((room) =>
      room.radiators.some((r) => r.radiatorId === message.radiatorId)
        ? {
          ...room,
          radiators: room.radiators.map((r) => r.radiatorId === message.radiatorId
            ? { ...r, currentOutput: message.currentOutput ?? null }
            : r),
        }
        : room));
  }, []);

  useEffect(() => {
    subscribe("radiator", applyRadiator);

    return () => {
      unsubscribe("radiator");
    };
  }, [subscribe, unsubscribe, applyRadiator]);

  if (rooms === null || home === undefined) {
    return <span>Loading...</span>;
  }

  if (rooms.length === 0) {
    return (
      <>
        <PageHeader title="Heat" />
        <HeatLoss home={home} rooms={rooms} />
        <FlowCard><div className="alert alert-info mb-0">There are no rooms, so there is nothing to draw. Add one!</div></FlowCard>
      </>
    );
  }

  if (home.heatMeterPower === null || home.heatMeterPower === undefined) {
    return (
      <>
        <PageHeader title="Heat" />
        <HeatLoss home={home} rooms={rooms} />
        <FlowCard><div className="alert alert-info mb-0">There is no heat meter reading, so there is nothing to divide up.</div></FlowCard>
      </>
    );
  }

  const meter = home.heatMeterPower / 1000;

  // Radiator and UFH outputs are forced to 0 W outside space heating, and the meter's heat goes to
  // the cylinder or back into the outdoor unit, so a diagram then would show the whole meter as
  // unaccounted.
  //
  const notHeating = home.heatingRunning !== true
    ? "The heat pump is not space heating"
    : home.dhwRunning === true
      ? "The heat pump is making hot water"
      : home.defrosting === true || meter < 0
        ? "The heat pump is defrosting"
        : null;

  if (notHeating) {
    return (
      <>
        <PageHeader title="Heat" />
        <HeatLoss home={home} rooms={rooms} />
        <FlowCard>
        <div className="alert alert-info mb-0">
          {notHeating}, so the radiators and underfloor heating are not being given any heat. The
          heat meter reads {watts(meter)}.
        </div>
        </FlowCard>
      </>
    );
  }

  // Radiators: the rooms' radiators, plus whatever the home's total has that no room claims.
  const roomRadiators = rooms.map(radiatorSum);
  const radiatorsInRooms = roomRadiators.reduce((s, v) => s + v, 0);
  const radiatorsUnassigned = Math.max((home.totalRadiatorOutput ?? 0) - radiatorsInRooms, 0);
  const radiatorTotal = radiatorsInRooms + radiatorsUnassigned;

  const hasUfh = (home.ufhManifoldCount ?? 0) > 0;
  const ufhKnown = hasUfh && home.totalUfhOutput !== null && home.totalUfhOutput !== undefined;
  const ufhTotal = ufhKnown ? home.totalUfhOutput as number : 0;
  const ufhUnassigned = ufhKnown ? (home.ufhUnassignedOutput ?? 0) as number : 0;

  const model = radiatorTotal + ufhTotal;
  const unaccounted = meter - model;

  // When the model claims more than the meter measured, the difference comes from a hatched node
  // beside the meter, split between radiators and UFH in proportion, so the columns still balance
  // and the overshoot is visible rather than absorbed.
  //
  const overMeter = unaccounted < 0 ? -unaccounted : 0;
  const meteredShare = model > 0 ? Math.min(meter / model, 1) : 1;

  const nodes: HeatNode[] = [
    { id: "meter", column: 0, label: "Heat meter", color: COLOR_METER },
  ];

  if (overMeter > 0) {
    nodes.push({ id: "over", column: 0, label: "Model over meter", color: COLOR_GREY, style: "hatched" });
  }

  nodes.push({ id: "radiators", column: 1, label: "Radiators", color: COLOR_RADIATORS });

  if (hasUfh) {
    nodes.push(ufhKnown
      ? { id: "ufh", column: 1, label: "UFH", color: COLOR_UFH }
      : { id: "ufh", column: 1, label: "UFH", color: COLOR_UFH, style: "dashed", minHeight: 28 });
  }

  if (unaccounted > 0) {
    nodes.push({ id: "unaccounted", column: 1, label: "Unaccounted", color: COLOR_GREY });
  }

  rooms.forEach((room) => {
    nodes.push({ id: `room-${room.roomId}`, column: 2, label: room.name, color: COLOR_ROOM });
  });

  if (radiatorsUnassigned > 0 || ufhUnassigned > 0) {
    nodes.push({ id: "unassigned", column: 2, label: "Not in a room", color: COLOR_GREY });
  }

  const links: HeatLink[] = [
    { source: "meter", target: "radiators", value: radiatorTotal * meteredShare, color: COLOR_METER },
    { source: "meter", target: "ufh", value: ufhTotal * meteredShare, color: COLOR_METER },
    { source: "meter", target: "unaccounted", value: Math.max(unaccounted, 0), color: COLOR_METER },
    { source: "over", target: "radiators", value: radiatorTotal * (1 - meteredShare), color: COLOR_GREY },
    { source: "over", target: "ufh", value: ufhTotal * (1 - meteredShare), color: COLOR_GREY },
    { source: "radiators", target: "unassigned", value: radiatorsUnassigned, color: COLOR_RADIATORS },
    { source: "ufh", target: "unassigned", value: ufhUnassigned, color: COLOR_UFH },
  ];

  rooms.forEach((room, i) => {
    links.push({ source: "radiators", target: `room-${room.roomId}`, value: roomRadiators[i], color: COLOR_RADIATORS });

    if (ufhKnown) {
      links.push({ source: "ufh", target: `room-${room.roomId}`, value: room.ufhInput ?? 0, color: COLOR_UFH });
    }
  });

  const columnNodes = Math.max(...[0, 1, 2].map((c) => nodes.filter((n) => n.column === c).length));
  const height = Math.max(360, columnNodes * 46);

  const layout = layoutSankey(nodes, links, {
    width: WIDTH,
    height,
    nodeWidth: NODE_WIDTH,
    gap: 12,
    marginLeft: MARGIN_LEFT,
    marginRight: MARGIN_RIGHT,
    minNodeHeight: 3,
  });

  const nodeLabel = (n: (typeof layout.nodes)[number]) => {
    if (n.style === "dashed") {
      return "unknown";
    }

    // The meter node's height is the larger of its reading and what it hands out, which is its
    // reading -- but say so explicitly rather than rely on that.
    return watts(n.id === "meter" ? meter : n.id === "over" ? overMeter : n.value);
  };

  const titleOf = (id: string) => nodes.find((n) => n.id === id)?.label ?? id;

  return (
    <>
      <PageHeader title="Heat" />
      <HeatLoss home={home} rooms={rooms} />
      <FlowCard>
      <div className="heat-sankey">
        <svg viewBox={`0 ${-LABEL_PAD} ${WIDTH} ${height + 2 * LABEL_PAD}`} role="img" aria-label="Heat from the heat meter to radiators, underfloor heating and rooms">
          <defs>
            <pattern id="heat-hatch" width="6" height="6" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">
              <rect width="6" height="6" fill="#ffffff" />
              <line x1="0" y1="0" x2="0" y2="6" stroke={COLOR_GREY} strokeWidth="3" />
            </pattern>
          </defs>

          {layout.links.map((link) => (
            <path key={`${link.source}-${link.target}`} d={link.path} fill={link.color} fillOpacity={0.35}>
              <title>{`${titleOf(link.source)} → ${titleOf(link.target)}: ${watts(link.value)}`}</title>
            </path>
          ))}

          {layout.nodes.map((n) => {
            const labelLeft = n.column === 0;
            const tx = labelLeft ? n.x - 6 : n.x + NODE_WIDTH + 6;
            const ty = n.y + n.height / 2;

            return (
              <g key={n.id}>
                <rect
                  x={n.x}
                  y={n.y}
                  width={NODE_WIDTH}
                  height={n.height}
                  fill={n.style === "hatched" ? "url(#heat-hatch)" : n.style === "dashed" ? "none" : n.color}
                  stroke={n.style ? n.color : "none"}
                  strokeDasharray={n.style === "dashed" ? "4,3" : undefined}
                />
                <text x={tx} y={ty} textAnchor={labelLeft ? "end" : "start"} className="heat-label">
                  <tspan x={tx} dy="-0.2em" className="heat-name">{n.label}</tspan>
                  <tspan x={tx} dy="1.2em" className="heat-value">{nodeLabel(n)}</tspan>
                </text>
              </g>
            );
          })}
        </svg>
      </div>

      <p className="text-body-secondary small mt-2 mb-0">
        The heat meter is measured. Radiators are calculated from their flow, return and room
        temperatures and their rating at dT50; UFH from its calibrated flow and the drop across the
        manifold. <strong>Unaccounted</strong> is heat the meter saw that the model does not place:
        pipework, emitters without probes, or model error.
        {overMeter > 0 && <> <strong>Model over meter</strong> is how far the model's total exceeds the meter.</>}
        {hasUfh && !ufhKnown && <> The UFH output is unknown, either because a manifold is not calibrated or because a probe has no reading. Rooms show their radiators only.</>}
      </p>
      </FlowCard>
    </>
  );
}

export default Heat;
