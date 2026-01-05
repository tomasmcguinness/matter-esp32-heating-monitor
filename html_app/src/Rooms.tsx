import { useContext, useEffect, useState } from "react"
import { NavLink, useNavigate } from "react-router"
import Temperature from "./Temperature.tsx";
import { WebSocketContext } from './WSContext.jsx';
import Power from "./Power.tsx";

function Rooms() {

  let navigate = useNavigate();

  let [roomList, setRoomList] = useState<any>([]);

  const { subscribe, unsubscribe } = useContext(WebSocketContext);

  useEffect(() => {

    subscribe("room", (message: any) => {
      setRoomList(roomList.map((a: any) => (a.roomId === message.roomId ? { ...a, temperature: message.temperature } : a)))
    })

    return () => {
      unsubscribe("room")
    }
  }, [subscribe, unsubscribe])

  useEffect(() => {
    const fetchRooms = async () => {
      // setRoomList([{
      //   "roomId": 1,
      //   "name": "Office",
      //   "temperature": 2039
      // }]);

      var response = await fetch("/api/rooms");

      if (response.ok) {
        let data = await response.json();
        setRoomList(data);
      }
    };

    fetchRooms();
  }, []);

  let rooms = roomList.map((n: any) => {
    return (<tr key={n.roomId} onClick={() => navigate(`/rooms/${n.roomId}`)} style={{ 'cursor': 'pointer' }}>
      <td>{n.roomId}</td>
      <td>{n.name}</td>
      <td><Temperature>{n.temperature}</Temperature></td>
      <td><Power>{n.heatLoss}</Power></td>
      <td><Power>{n.combinedRadiatorOutput}</Power></td>
      </tr>);
  });

  if (rooms.length == 0) {
    return (<div className="alert alert-info">There are no rooms</div>)
  }

  return (
    <>
      <h1>Rooms</h1>
      <hr />
      <table className="table table-striped table-bordered">
        <thead>
          <tr>
            <th style={{ width: 'auto' }}>ID</th>
            <th>Name</th>
            <th>Temperature</th>
            <th>Heat Loss</th>
            <th>Radiator Output</th>
          </tr>
        </thead>
        <tbody>
          {rooms}
        </tbody>
      </table>
      <NavLink className="btn btn-primary" to="/rooms/add">Add Room</NavLink>
    </>
  )
}

export default Rooms;
