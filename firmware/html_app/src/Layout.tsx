import { useCallback, useContext, useEffect, useState } from "react";
import { WebSocketContext } from "./WSContext.jsx";
import Temperature from "./Temperature";
import Power from "./Power";
import "./Layout.css";

// The heating circuit drawn as a schematic: a flow trunk across the top, a return trunk across the
// bottom, and one radiator dropped between them per emitter. Geometry comes from the supplied
// mock-up; everything in it is driven by GET /api/rooms, which carries each room's radiators inline.

type Radiator = {
  radiatorId: number;
  name: string;
  type: number;
  flowTemp: number;
  returnTemp: number;
  currentOutput: number;
};

type Room = {
  roomId: number;
  name: string;
  currentTemperature: number;
  targetTemperature: number;
  radiators: Radiator[];
};

// Only the fields the heat meter panel reads. GET /api/home and the "home" push carry a good deal
// more, built by the same build_home_json(), and all three of these are nullable there.
//
type HomeSnapshot = {
  heatMeterPower?: number | null;
  heatMeterFlowTemperature?: number | null;
  heatMeterReturnTemperature?: number | null;
};

// The "radiator" push carries whichever of the two temperatures the sensor reported, never both,
// plus the output recalculated from it. The "room" push carries only the temperature.
//
type RadiatorPush = {
  radiatorId: number;
  flowTemp?: number;
  returnTemp?: number;
  currentOutput?: number;
};

type RoomPush = {
  roomId: number;
  temperature: number;
};

// Mock-up geometry. The trunks, the elbows and the heat meter are fixed; everything to the right of
// x = FIRST_COL_X is laid out from the radiator count, so the canvas grows with the system.
//
const COL_PITCH = 84;
const FIRST_COL_X = 420;
const GROUP_GAP = 40; // extra space between rooms, so the group brackets read as distinct

const FLOW_Y = 140;
const RETURN_Y = 600;
const RAD_TOP = 340;
const RAD_BOTTOM = 430;
const RAD_HALF_WIDTH = 27;
const FIN_TOP = 348;
const FIN_BOTTOM = 422;
const BRACKET_Y = 118; // between the target label (top 92) and the flow trunk

// The two legs running to and from the plant, and the heat meter spliced across them. The meter's
// white fill hides the legs where they cross it, so it reads as an instrument in the pipework --
// which only works if its Flow and Return rows sit on the legs they are describing. Those rows are
// offset from the box top by the difference, so the alignment holds if any of these numbers move.
//
const PLANT_FLOW_Y = 300;
const PLANT_RETURN_Y = 340;

const METER_X = 196;
const METER_Y = 258;
const METER_W = 148;
const METER_H = 124;

const METER_FLOW_ROW_Y = PLANT_FLOW_Y - METER_Y;
const METER_RETURN_ROW_Y = PLANT_RETURN_Y - METER_Y;

const CANVAS_HEIGHT = 670;
const SVG_HEIGHT = 650;

type Column = { cx: number; radiator: Radiator | null };
type Group = { room: Room; columns: Column[] };

// The transient temperature and output fields are plain integers that start at zero and are only
// written once a sensor reports, and nothing in the payload separates "not reported" from a real
// reading. Indoors, an exact 0.00 °C is the sentinel in practice -- so treat it as absent and let
// the formatters render a dash rather than claiming the room is at freezing.
//
function reading(value: number | null | undefined) {
  if (value === null || value === undefined || value === 0) {
    return undefined;
  }

  return value;
}

// The heat meter reports in milliwatts, and the panel wants "6.1 kW" rather than the whole-watt
// figure ElectricalPower produces, so this is the one place a formatter is not reused.
//
function kilowatts(milliwatts: number | null | undefined) {
  if (milliwatts === null || milliwatts === undefined) {
    return "-";
  }

  return (milliwatts / 1000000.0).toFixed(1);
}

function buildGroups(rooms: Room[]): Group[] {
  let x = FIRST_COL_X;

  return rooms.map((room) => {
    const radiators = [...room.radiators].sort((a, b) => a.radiatorId - b.radiatorId);

    // A room with no radiators still gets a slot, so it keeps its header instead of vanishing from
    // the schematic. Its column carries no radiator and draws no drop.
    //
    const columns: Column[] = (radiators.length > 0 ? radiators : [null]).map((radiator) => {
      const cx = x;
      x += COL_PITCH;
      return { cx, radiator };
    });

    x += GROUP_GAP;

    return { room, columns };
  });
}

function Layout() {

  const [rooms, setRooms] = useState<Room[] | null>(null);
  const [home, setHome] = useState<HomeSnapshot>({});

  const { subscribe, unsubscribe } = useContext(WebSocketContext);

  // The firmware builds the "home" push from the same function as GET /api/home, so the snapshot
  // replaces wholesale -- the same applier Home.tsx uses.
  //
  const applyHome = useCallback((data: HomeSnapshot) => {
    setHome(data);
  }, []);

  // The radiator channel pushes whichever of flowTemp / returnTemp changed, plus the recalculated
  // currentOutput. Patch immutably inside the updater rather than closing over the list, so the
  // handler registered at mount never works against a stale snapshot.
  //
  const applyRadiator = useCallback((message: RadiatorPush) => {
    setRooms((previous) => {
      if (previous === null) {
        return previous;
      }

      return previous.map((room) => {
        if (!room.radiators.some((r) => r.radiatorId === message.radiatorId)) {
          return room;
        }

        return {
          ...room,
          radiators: room.radiators.map((r) => r.radiatorId === message.radiatorId
            ? {
              ...r,
              ...(message.flowTemp !== undefined ? { flowTemp: message.flowTemp } : {}),
              ...(message.returnTemp !== undefined ? { returnTemp: message.returnTemp } : {}),
              ...(message.currentOutput !== undefined ? { currentOutput: message.currentOutput } : {}),
            }
            : r),
        };
      });
    });
  }, []);

  const applyRoom = useCallback((message: RoomPush) => {
    setRooms((previous) => {
      if (previous === null) {
        return previous;
      }

      return previous.map((room) => room.roomId === message.roomId
        ? { ...room, currentTemperature: message.temperature }
        : room);
    });
  }, []);

  useEffect(() => {
    const fetchData = async () => {
      const [roomsResponse, homeResponse] = await Promise.all([
        fetch("/api/rooms"),
        fetch("/api/home"),
      ]);

      if (roomsResponse.ok) {
        const data: Room[] = await roomsResponse.json();
        setRooms(data.sort((a, b) => a.name.localeCompare(b.name)));
      }

      if (homeResponse.ok) {
        setHome(await homeResponse.json());
      }
    };

    fetchData();
  }, []);

  useEffect(() => {
    subscribe("home", applyHome);
    subscribe("radiator", applyRadiator);
    subscribe("room", applyRoom);

    return () => {
      unsubscribe("home");
      unsubscribe("radiator");
      unsubscribe("room");
    };
  }, [subscribe, unsubscribe, applyHome, applyRadiator, applyRoom]);

  if (rooms === null) {
    return <span>Loading...</span>;
  }

  if (rooms.length === 0) {
    return (
      <>
        <h1>Layout</h1>
        <hr />
        <div className="alert alert-info">There are no rooms, so there is nothing to draw. Add one!</div>
      </>
    );
  }

  const groups = buildGroups(rooms);

  const allColumns = groups.flatMap((g) => g.columns);
  const lastColumnX = allColumns[allColumns.length - 1].cx;

  const trunkEndX = lastColumnX + 56;
  const canvasWidth = lastColumnX + 80;

  return (
    <>
      <h1>Layout</h1>
      <hr />
      <div className="layout-schematic">
        <div className="legend">
          <span><span className="swatch-flow"></span>Flow</span>
          <span><span className="swatch-return"></span>Return</span>
          <span><span style={{ color: '#c94f24', fontWeight: 700 }}>&#9632;</span>Flow temp</span>
          <span><span style={{ color: '#2f6fed', fontWeight: 700 }}>&#9632;</span>Return temp</span>
        </div>

        <div className="scroll">
          <div className="canvas" style={{ width: `${canvasWidth}px`, height: `${CANVAS_HEIGHT}px` }}>
            <svg width={canvasWidth} height={SVG_HEIGHT} viewBox={`0 0 ${canvasWidth} ${SVG_HEIGHT}`} style={{ display: 'block' }}>
              {/* Flow from the plant, up and across the top */}
              <line x1="60" y1={PLANT_FLOW_Y} x2="360" y2={PLANT_FLOW_Y} stroke="#1a1a1a" strokeWidth="3" />
              <line x1="360" y1={PLANT_FLOW_Y} x2="360" y2={FLOW_Y} stroke="#1a1a1a" strokeWidth="3" />
              <line x1="360" y1={FLOW_Y} x2={trunkEndX} y2={FLOW_Y} stroke="#1a1a1a" strokeWidth="3" />

              {/* Return across the bottom, down and back to the plant */}
              <line x1="360" y1={RETURN_Y} x2={trunkEndX} y2={RETURN_Y} stroke="#555555" strokeWidth="2" strokeDasharray="6,5" />
              <line x1="360" y1={RETURN_Y} x2="360" y2={PLANT_RETURN_Y} stroke="#555555" strokeWidth="2" strokeDasharray="6,5" />
              <line x1="360" y1={PLANT_RETURN_Y} x2="60" y2={PLANT_RETURN_Y} stroke="#555555" strokeWidth="2" strokeDasharray="6,5" />

              <text x="62" y={PLANT_FLOW_Y - 8} fontSize="10" letterSpacing="0.6" fill="#9a9a9a">FLOW FROM PLANT</text>
              <text x="62" y={PLANT_RETURN_Y + 20} fontSize="10" letterSpacing="0.6" fill="#9a9a9a">RETURN TO PLANT</text>

              <rect x={METER_X} y={METER_Y} width={METER_W} height={METER_H} rx="6" fill="#ffffff" stroke="#c94f24" strokeWidth="1.5" />

              {groups.map(({ room, columns }) => {
                const firstCx = columns[0].cx;
                const lastCx = columns[columns.length - 1].cx;

                return (
                  <g key={room.roomId}>
                    {/* Only a room split across several radiators needs a bracket to group them. */}
                    {columns.length > 1 && (
                      <>
                        <line x1={firstCx} y1={BRACKET_Y} x2={lastCx} y2={BRACKET_Y} stroke="#c9c9c9" strokeWidth="1" />
                        {columns.map((column) => (
                          <line key={column.cx} x1={column.cx} y1={BRACKET_Y} x2={column.cx} y2={BRACKET_Y + 8} stroke="#c9c9c9" strokeWidth="1" />
                        ))}
                      </>
                    )}

                    {columns.map(({ cx, radiator }) => radiator === null ? null : (
                      <g key={radiator.radiatorId}>
                        <line x1={cx} y1={FLOW_Y} x2={cx} y2={RAD_TOP} stroke="#1a1a1a" strokeWidth="3" />
                        <line x1={cx} y1={RAD_BOTTOM} x2={cx} y2={RETURN_Y} stroke="#555555" strokeWidth="2" strokeDasharray="6,5" />
                        <rect x={cx - RAD_HALF_WIDTH} y={RAD_TOP} width={RAD_HALF_WIDTH * 2} height={RAD_BOTTOM - RAD_TOP} fill="#ffffff" stroke="#1a1a1a" strokeWidth="2" />
                        <line x1={cx - 13} y1={FIN_TOP} x2={cx - 13} y2={FIN_BOTTOM} stroke="#1a1a1a" strokeWidth="1.5" />
                        <line x1={cx} y1={FIN_TOP} x2={cx} y2={FIN_BOTTOM} stroke="#1a1a1a" strokeWidth="1.5" />
                        <line x1={cx + 13} y1={FIN_TOP} x2={cx + 13} y2={FIN_BOTTOM} stroke="#1a1a1a" strokeWidth="1.5" />
                      </g>
                    ))}
                  </g>
                );
              })}
            </svg>

            {/* Each row is centred on the leg it describes, so the reading lines up with the pipe it
                came off. The kW figure sits below both rather than under the title, because the
                space above the flow leg is only tall enough for the panel's label. */}
            <div className="meter" style={{ left: `${METER_X}px`, top: `${METER_Y}px`, width: `${METER_W}px`, height: `${METER_H}px` }}>
              <div className="meter-title">Heat meter</div>
              <div className="meter-row" style={{ top: `${METER_FLOW_ROW_Y}px` }}>
                <span className="k">Flow</span><span className="flow"><Temperature>{reading(home.heatMeterFlowTemperature)}</Temperature></span>
              </div>
              <div className="meter-row" style={{ top: `${METER_RETURN_ROW_Y}px` }}>
                <span className="k">Return</span><span className="ret"><Temperature>{reading(home.heatMeterReturnTemperature)}</Temperature></span>
              </div>
              <div className="meter-power">{kilowatts(home.heatMeterPower)} <span>kW</span></div>
            </div>

            {groups.map(({ room, columns }) => {
              const centreX = (columns[0].cx + columns[columns.length - 1].cx) / 2;

              return (
                <div key={room.roomId}>
                  <div className="lbl name" style={{ left: `${centreX}px` }}>{room.name}</div>
                  <div className="lbl room-temp" style={{ left: `${centreX}px` }}>
                    <Temperature>{reading(room.currentTemperature)}</Temperature>
                  </div>
                  <div className="lbl room-target" style={{ left: `${centreX}px` }}>
                    target <Temperature>{reading(room.targetTemperature)}</Temperature>
                  </div>

                  {columns.map(({ cx, radiator }) => radiator === null ? (
                    <div key="none" className="lbl no-emitters" style={{ left: `${cx}px` }}>no radiators</div>
                  ) : (
                    <div key={radiator.radiatorId}>
                      {/* Without a name the columns of a multi-radiator room are indistinguishable.
                          A room with one radiator reads fine from its own header, as in the mock-up. */}
                      {columns.length > 1 && (
                        <div className="lbl rad-name" style={{ left: `${cx}px` }}>{radiator.name}</div>
                      )}
                      <div className="lbl flow-temp" style={{ left: `${cx}px` }}>
                        <Temperature>{reading(radiator.flowTemp)}</Temperature>
                      </div>
                      <div className="lbl watt" style={{ left: `${cx}px` }}>
                        <Power>{radiator.currentOutput}</Power>
                      </div>
                      <div className="lbl ret-temp" style={{ left: `${cx}px` }}>
                        <Temperature>{reading(radiator.returnTemp)}</Temperature>
                      </div>
                    </div>
                  ))}
                </div>
              );
            })}
          </div>
        </div>
      </div>
    </>
  )
}

export default Layout;
