import { useEffect, useState } from "react"
import { NavLink, useNavigate } from "react-router"
import useWebSocket from 'react-use-websocket';
import Temperature from "./Temperature.tsx";

function Rooms() {

  let navigate = useNavigate();

  let [roomList, setRoomList] = useState<any>([]);

  var url = new URL('/ws', window.location.href);

  url.protocol = url.protocol.replace('http', 'ws');

  const socketUrl = url.href; 

  const {
    sendJsonMessage,
    lastJsonMessage,
  } = useWebSocket(socketUrl, {
    onOpen: () => {
      sendJsonMessage({ radiators: true });
      console.log('opened');
    },
    shouldReconnect: (_) => true,
    share: true
  });

  useEffect(() => {
    console.log("Web socket data has changed...." + lastJsonMessage);

    if (!lastJsonMessage ||  (lastJsonMessage as any).type !== "room_temperature") {
      return;
    }

    setRoomList(roomList.map((a:any) => (a.room_id ===  (lastJsonMessage as any).id ? {...a, temperature:  (lastJsonMessage as any).temperature} : a)))

  }, [lastJsonMessage]);

  useEffect(() => {
    const fetchRooms = async () => {
      setRoomList([{
		"roomId":	1,
		"name":	"Office",
		"temperature":	2039
	}]);

      // var response = await fetch("http://192.168.1.118/api/rooms");

      // if (response.ok) {
      //   let data = await response.json();
      //   setRoomList(data);
      // }
    };

    fetchRooms();
  }, []);

  let rooms = roomList.map((n: any) => {
    return (<tr key={n.roomId} onClick={() => navigate(`/rooms/${n.roomId}`)} style={{ 'cursor': 'pointer' }}><td>{n.roomId}</td><td>{n.name}</td><td><Temperature>{n.temperature}</Temperature></td></tr>);
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
            <th>Temperature</th>
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
