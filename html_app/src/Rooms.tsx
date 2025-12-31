import { useEffect, useState } from "react"
import { NavLink, useNavigate } from "react-router"
import useWebSocket from 'react-use-websocket';

function Rooms() {

  let navigate = useNavigate();

  let [roomList, setRoomList] = useState<any>([]);

  const socketUrl = 'ws://192.168.1.104/ws';

  const {
    sendJsonMessage,
    lastJsonMessage,
  } = useWebSocket(socketUrl, {
    onOpen: () => { sendJsonMessage({ radiators: true }); console.log('opened'); },
    shouldReconnect: (_) => true,
    share: true
  });

  useEffect(() => {
    console.log("Web socket data has changed...." + lastJsonMessage);

    if (!lastJsonMessage) {
      return;
    }

  }, [lastJsonMessage]);

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

  let rooms = roomList.map((n: any) => {
    return (<tr key={n.roomId} onClick={() => navigate(`/rooms/${n.roomId}`)} style={{ 'cursor': 'pointer' }}><td>{n.radiatorId}</td><td>{n.name}</td></tr>);
  });

  return (
    <>
      <h1>Rooms</h1>
      <hr />
      <table className="table table-striped table-bordered">
        <thead>
          <tr>
            <th style={{ width: 'auto' }}>ID</th>
            <th>Name</th>
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
