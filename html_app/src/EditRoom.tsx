import { useEffect, useState } from "react";
import { NavLink, useParams } from "react-router";

function EditRoom() {

  let { roomId } = useParams();
  let [radiators, setRadiators] = useState<any>([]);
  let [room, setRoom] = useState<any>(null);
  let [heatLossPerDegree, setHeatLossPerDegree] = useState<string | undefined>(undefined);

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
        setHeatLossPerDegree(data.heatLossPerDegree);
        setRoom(data);
      }
    };

    fetchRoom();
  }, [roomId]);

  let saveRoom = async () => {

    const radiatorIds = [...document.querySelectorAll('.inp:checked')].map((e: any) => parseInt(e.value));

    var json = JSON.stringify({
      heatLossPerDegree: parseInt(heatLossPerDegree!),
      radiatorIds
    });

    fetch(`/api/rooms/${roomId}`, { method: "PUT", headers: { 'Content-Type': 'application/json' }, body: json });

  };

  let radiatorList = radiators.map((n: any) => {

    return <li key={n.radiatorId} className="list-group-item">
      <input className="form-check-input me-1 inp" type="checkbox" value={n.radiatorId} id={n.radiatorId} checked={n.checked} />
      <label className="form-check-label" htmlFor={n.radiatorId}>{n.name}</label>
    </li>
  });

  if (!room) {
    return <span>Loading room...</span>;
  }

  return (
    <>
      <h1>Update {room.name}</h1>
      <hr />
      <div className="mb-3">
        <label htmlFor="heatLoss" className="form-label">Heat Loss Per °C<span style={{ 'color': 'red' }}>*</span></label>
        <input type="number" name="heatLoss" maxLength={20} className="form-control" id="heatLoss" placeholder="25" required={true} value={heatLossPerDegree || ''} onChange={(e) => setHeatLossPerDegree(e.target.value)} />
      </div>
      <label htmlFor="heatLoss" className="form-label">Radiators <span style={{ 'color': 'red' }}>*</span></label>
      <ul className="list-group">
        {radiatorList}
      </ul>
      <button className="btn btn-primary" onClick={saveRoom} style={{ 'marginRight': '5px' }}>Save</button>
      <NavLink className="btn btn-danger" to={`/rooms/${roomId}`}>Cancel</NavLink>
    </>
  )
}

export default EditRoom;
