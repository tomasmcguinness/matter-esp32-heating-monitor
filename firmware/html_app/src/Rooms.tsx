import { useContext, useEffect, useState } from "react"
import { NavLink, useNavigate } from "react-router"
import Temperature from "./Temperature.tsx";
import { WebSocketContext } from './WSContext.jsx';
import Power from "./Power.tsx";
import PageHeader from "./PageHeader";
import { ComfortBar, ComfortGrid, ComfortLegend, ComfortScopeToggle } from "./Comfort";
import { COMFORT_DAYS, COMFORT_SUMMARY_DAYS, formatOffset, latestDate, recentRecords, useComfort } from "./comfortData";
import { summariseComfort, type ComfortScope } from "./historyData";

function Rooms() {

  let navigate = useNavigate();

  let [roomList, setRoomList] = useState<any>([]);
  let [homeData, setHomeData] = useState<any>({});
  const [scope, setScope] = useState<ComfortScope>("all");
  // "name", or by the last week's average distance from target, warmest or coldest first.
  const [sortBy, setSortBy] = useState<"name" | "warmest" | "coldest">("name");

  const { body: comfort } = useComfort(COMFORT_DAYS);
  const comfortRecords = comfort?.records ?? [];
  const comfortToday = latestDate(comfortRecords);
  const band = comfort?.bandC100 ?? 20;

  const { subscribe, unsubscribe } = useContext(WebSocketContext);

  useEffect(() => {

    subscribe("room", (message: any) => {
      console.log({ message });
      // var updatedRooms = roomList.map((room: any) => (room.roomId === message.roomId ? { ...room, temperature: message.temperature } : room));
      // console.log({updatedRooms});
      // setRoomList(updatedRooms);
    })

    return () => {
      unsubscribe("room")
    }
  }, [subscribe, unsubscribe])

  useEffect(() => {
    const fetchData = async () => {
      const [roomsResponse, homeResponse] = await Promise.all([
        fetch("/api/rooms"),
        fetch("/api/home"),
      ]);

      if (roomsResponse.ok) {
        setRoomList(await roomsResponse.json());
      }

      if (homeResponse.ok) {
        setHomeData(await homeResponse.json());
      }
    };

    fetchData();
  }, []);

  // heatLossDifference is a ratio, measured / predicted x 100, so 112 is a measured loss 12% above
  // the prediction and 85 one 15% below it. Shown as that signed difference, never as the ratio,
  // which read as a difference with no direction.
  const difference = (ratio: number | null | undefined) => {
    if (ratio == null || ratio === 0) {
      return '-';
    }

    const pct = Math.round(ratio - 100);

    if (pct === 0) {
      return <span title="Measured heat loss matches the prediction">±0%</span>;
    }

    // Up and red: the room loses more heat than predicted. Down and green: less. Inline SVG (Bootstrap
    // Icons' caret-up-fill / caret-down-fill) because the icon font is not part of the embedded bundle.
    return (
      <span title={`Measured heat loss is ${Math.abs(pct)}% ${pct > 0 ? 'above' : 'below'} the prediction`}>
        <svg xmlns="http://www.w3.org/2000/svg" width="12" height="12" fill="currentColor" viewBox="0 0 16 16" aria-hidden="true"
          className={pct > 0 ? 'text-danger' : 'text-success'} style={{ 'verticalAlign': 'baseline' }}>
          {pct > 0
            ? <path d="m7.247 4.86-4.796 5.481c-.566.647-.106 1.659.753 1.659h9.592a1 1 0 0 0 .753-1.659l-4.796-5.48a1 1 0 0 0-1.506 0z" />
            : <path d="M7.247 11.14 2.451 5.658C1.885 5.013 2.345 4 3.204 4h9.592a1 1 0 0 1 .753 1.659l-4.796 5.48a1 1 0 0 1-1.506 0z" />}
        </svg>
        {' '}{Math.abs(pct)}%
        <span className="visually-hidden">{pct > 0 ? ' above prediction' : ' below prediction'}</span>
      </span>
    );
  };

  let totalPredictedHeatLoss = homeData.totalPredictedHeatLoss ?? 0;
  let totalMeasuredHeatLoss = homeData.totalMeasuredHeatLoss ?? 0;

  // GET /api/rooms already returns these in name order; sorted again here so the view does not
  // depend on that, on a copy because .sort() would otherwise reorder the state array in place.
  // The last week of time at target for each room, from the board's own today.
  const weekOf = (roomId: number) =>
    summariseComfort(comfortToday ? recentRecords(comfortRecords, roomId, comfortToday, COMFORT_SUMMARY_DAYS) : [], scope);

  // Rooms with no figure go last whichever way the column is sorted.
  const byOffset = (a: any, b: any, sign: number) => {
    const ma = weekOf(a.roomId).meanC100;
    const mb = weekOf(b.roomId).meanC100;
    if (ma === null || mb === null) {
      return ma === null ? (mb === null ? a.name.localeCompare(b.name) : 1) : -1;
    }
    return sign * (mb - ma);
  };

  const sorted = [...roomList].sort((a: any, b: any) =>
    sortBy === "name" ? a.name.localeCompare(b.name) : byOffset(a, b, sortBy === "warmest" ? 1 : -1));

  const cycleSort = () => setSortBy(sortBy === "name" ? "warmest" : sortBy === "warmest" ? "coldest" : "name");

  let rooms = sorted.map((n: any) => {
    const week = weekOf(n.roomId);
    return (<tr key={n.roomId} onClick={() => navigate(`/rooms/${n.roomId}`)} style={{ 'cursor': 'pointer' }}>
      <td>{n.name}</td>
      <td><Temperature>{n.targetTemperature}</Temperature></td>
      <td><Temperature>{n.currentTemperature}</Temperature></td>
      <td style={{ verticalAlign: 'middle' }}><ComfortBar summary={week} /></td>
      <td title={week.estimated ? "Partly judged against today's target: the target was not recorded before 28 November" : undefined}>
        {formatOffset(week.meanC100)}{week.estimated && week.meanC100 !== null ? '*' : ''}
      </td>
      <td><Power>{n.heatInput}</Power></td>
      <td><Power>{n.predictedHeatLoss}</Power></td>      
      <td><Power>{n.measuredHeatLoss}</Power></td>
      <td>{difference(n.heatLossDifference)}</td>
    </tr>);
  });

  return (
    <>
      <PageHeader title="Rooms">
        <NavLink className="btn btn-primary" to="/rooms/add">Add Room</NavLink>
      </PageHeader>
      {rooms.length === 0 && <div className="alert alert-info">There are no rooms. Add one!</div>}
      {rooms.length > 0 && <table className="table table-striped table-bordered">
        <thead>
          <tr>
            <th>Name</th>
            <th>Target Temp</th>
            <th>Current Temp</th>
            <th title="Time under, on and over target over the last 7 days">Last 7 Days</th>
            <th role="button" onClick={cycleSort} style={{ cursor: 'pointer', whiteSpace: 'nowrap' }}
              title="Average distance from target over the last 7 days. Click to sort warmest first, then coldest first, then by name.">
              Avg vs Target {sortBy === 'warmest' ? '▼' : sortBy === 'coldest' ? '▲' : ''}
            </th>
            <th>Heat Input</th>
            <th>Predicted Heat Loss</th>
            <th>Measured Heat Loss</th>
            <th title="Measured heat loss against the prediction: + means the room loses more heat than predicted">Measured vs Predicted</th>
          </tr>
        </thead>
        <tbody>
          {rooms}
        </tbody>
        <tfoot>
          <tr>
            <th>Totals</th>
            <th></th>
            <th></th>
            <th></th>
            <th></th>
            <th></th>
            <th><Power>{totalPredictedHeatLoss}</Power></th>
            <th><Power>{totalMeasuredHeatLoss}</Power></th>
            <th></th>
          </tr>
        </tfoot>
      </table>}

      {rooms.length > 0 && comfortToday && (
        <div className="card" style={{ marginBottom: '20px' }}>
          <div className="card-header d-flex justify-content-between align-items-center flex-wrap gap-2">
            <span>Distance from target, last 4 weeks</span>
            <ComfortScopeToggle scope={scope} setScope={setScope} />
          </div>
          <div className="card-body">
            <ComfortGrid rooms={sorted} records={comfortRecords} today={comfortToday} scope={scope} band={band} />
            <p className="text-muted small mt-2 mb-0">
              Each square is the room's average distance from its target that day: blue colder, red warmer, grey
              within {(band / 100).toFixed(1)} °C. Today, so far, is paler. <strong>While heating</strong> counts only
              the time the heat pump was space heating, so a room that cools overnight with the heating off does not
              read as cold. The table's Last 7 Days and Avg vs Target follow the same choice. Days before 28 November
              are judged against each room's target now (marked *), since targets were not recorded until then.
            </p>
            <div className="mt-1"><ComfortLegend band={band} /></div>
          </div>
        </div>
      )}
    </>
  )
}

export default Rooms;
