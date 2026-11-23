import { useContext, useEffect, useState } from "react"
import { NavLink, useNavigate } from "react-router"
import Temperature from "./Temperature.tsx";
import { WebSocketContext } from './WSContext.jsx';
import Power from "./Power.tsx";
import PageHeader from "./PageHeader";

function Rooms() {

  let navigate = useNavigate();

  let [roomList, setRoomList] = useState<any>([]);
  let [homeData, setHomeData] = useState<any>({});

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
  let rooms = [...roomList].sort((a: any, b: any) => a.name.localeCompare(b.name)).map((n: any) => {
    return (<tr key={n.roomId} onClick={() => navigate(`/rooms/${n.roomId}`)} style={{ 'cursor': 'pointer' }}>
      <td>{n.name}</td>
      <td><Temperature>{n.targetTemperature}</Temperature></td>
      <td><Temperature>{n.currentTemperature}</Temperature></td>
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
            <th><Power>{totalPredictedHeatLoss}</Power></th>
            <th><Power>{totalMeasuredHeatLoss}</Power></th>
            <th></th>
          </tr>
        </tfoot>
      </table>}

    </>
  )
}

export default Rooms;
