import { useEffect, useState } from "react";
import { NavLink, useParams } from "react-router";

function EditRoom() {

  let { roomId } = useParams();
  let [radiators, setRadiators] = useState<any>([]);
  let [room, setRoom] = useState<any>(null);

  useEffect(() => {
    const fetchRadiators = async () => {
      var response = await fetch("/api/radiators");

      if (response.ok) {
        let data = await response.json();
        setRadiators(data);
      }
    };

    fetchRadiators();
  }, []);

  useEffect(() => {
    const fetchRoom = async () => {
      var response = await fetch(`/api/rooms/${roomId}`);

      if (response.ok) {
        let data = await response.json();
        setRoom(data);
      }
    };

    fetchRoom();
  }, [roomId]);

  let saveRoom = async () => {

    const radiatorIds = [...document.querySelectorAll('.inp:checked')].map((e: any) => parseInt(e.value));

    var json = JSON.stringify({
      radiatorIds
    });

    fetch(`/api/rooms/${roomId}`, { method: "PUT", headers: { 'Content-Type': 'application/json' }, body: json });

  };

  let radiatorList = radiators.map((n: any) => {

    return <li key={n.radiatorId} className="list-group-item">
      <input className="form-check-input me-1 inp" type="checkbox" value={n.radiatorId} id={n.radiatorId} />
      <label className="form-check-label" htmlFor={n.radiatorId}>{n.name}</label>
    </li>
  });

  if (!room) {
    return <span>Loading...</span>;
  }

  return (
    <>
      <h1>Update {room.name}</h1>
      <hr />
      <ul className="list-group">
        {radiatorList}
      </ul>
      <button className="btn btn-primary" onClick={saveRoom} style={{ 'marginRight': '5px' }}>Save</button>
      <NavLink className="btn btn-danger" to={`/rooms/${roomId}`}>Cancel</NavLink>
    </>
  )
}

export default EditRoom;
