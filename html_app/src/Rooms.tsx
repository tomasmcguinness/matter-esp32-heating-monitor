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
    const fetchRooms = async () => {

      var response = await fetch("/api/rooms");

      if (response.ok) {
        let data = await response.json();
        setRoomList(data);
      }
    };

    fetchRooms();
  }, []);

  // TODO Sort by name
  let rooms = roomList.sort((a: any, b: any) => a.name.localeCompare(b.name)).map((n: any) => {
    return (<tr key={n.roomId} onClick={() => navigate(`/rooms/${n.roomId}`)} style={{ 'cursor': 'pointer' }}>
      <td>{n.name}</td>
      <td><Temperature>{n.temperature}</Temperature></td>
      <td><Power>{n.heatLoss}</Power></td>
      <td><Power>{n.combinedRadiatorOutput}</Power></td>
    </tr>);
  });

  return (
    <>
      <h1>Rooms <NavLink className="btn btn-primary action-button" to="/rooms/add">Add Room</NavLink></h1>
      <hr />
      {rooms.length === 0 && <div className="alert alert-info">There are no rooms. Add one!</div>}
      {rooms.length > 0 && <table className="table table-striped table-bordered">
        <thead>
          <tr>
            <th>Name</th>
            <th>Temperature</th>
            <th>Est. Heat Loss</th>
            <th>Radiator Output</th>
            <th>Act. Heat Loss</th>
          </tr>
        </thead>
        <tbody>
          {rooms}
        </tbody>
      </table>}
      
    </>
  )
}

export default Rooms;
