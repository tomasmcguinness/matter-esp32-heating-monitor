import { useEffect, useState } from "react"
import { NavLink, useParams, useNavigate } from "react-router"
import SensorSelect from "./SensorSelect"

function EditRoom() {

  const { roomId } = useParams();

  const navigate = useNavigate();

  const [radiators, setRadiators] = useState<any>([]);
  
  const [name, setName] = useState<string>('');
  const [heatLossPerDegree, setHeatLossPerDegree] = useState<number>(0);
  const [temperatureSensor, setTemperatureSensor] = useState<string>('0|0');

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
        setName(data.name);
        setHeatLossPerDegree(data.heatLossPerDegree);
        setTemperatureSensor(`${data.temperatureSensorNodeId}|${data.temperatureSensorEndpointId}`);
      }
    };

    fetchRoom();
  }, [roomId]);

  let saveRoom = async () => {

    const radiatorIds = [...document.querySelectorAll('.inp:checked')].map((e: any) => parseInt(e.value));

    var temperatureSensorNodeId = parseInt(temperatureSensor!.split('|')[0]);
    var temperatureSensorEndpointId = parseInt(temperatureSensor!.split('|')[1]);

    var json = JSON.stringify({
      name,
      heatLossPerDegree,
      radiatorIds,
      temperatureSensorNodeId,
      temperatureSensorEndpointId,
    });

    fetch(`/api/rooms/${roomId}`, { method: "PUT", headers: { 'Content-Type': 'application/json' }, body: json }).then(r => {
      if (r.ok) {
        navigate(`/rooms/${roomId}`);
      } else {
        alert("Failed to update room");
      }
    });

  };

  let radiatorList = radiators.map((n: any) => {

    return <li key={n.radiatorId} className="list-group-item">
      <input className="form-check-input me-1 inp" type="checkbox" value={n.radiatorId} id={n.radiatorId} checked={n.checked} />
      <label className="form-check-label" htmlFor={n.radiatorId}>{n.name}</label>
    </li>
  });

  return (
    <>
      <h1>Update {name}</h1>
      <hr />
      <div className="mb-3">
        <label htmlFor="name" className="form-label">Name<span style={{ 'color': 'red' }}>*</span></label>
        <input type="string" name="name" maxLength={20} className="form-control" id="name" placeholder="Room Name e.g. Office" required={true} value={name} onChange={(e) => setName(e.target.value)} />
      </div>
      <div className="mb-3">
        <label htmlFor="heatLoss" className="form-label">Heat Loss Per °C<span style={{ 'color': 'red' }}>*</span></label>
        <input type="number" name="heatLoss" maxLength={20} className="form-control" id="heatLoss" placeholder="25" required={true} value={heatLossPerDegree || ''} onChange={(e) => setHeatLossPerDegree(parseInt(e.target.value))} />
      </div>
      <div className="mb-3">
          <SensorSelect title="Room Temperature Sensor" selectedSensor={temperatureSensor} onSelectedSensorChange={(e) => setTemperatureSensor(e)} />
      </div>
      <div className="mb-3">
        <label htmlFor="heatLoss" className="form-label">Radiators <span style={{ 'color': 'red' }}>*</span></label>
        <ul className="list-group">
          {radiatorList}
        </ul>
      </div>
      <button className="btn btn-primary" onClick={saveRoom} style={{ 'marginRight': '5px' }}>Save</button>
      <NavLink className="btn btn-danger" to={`/rooms/${roomId}`}>Cancel</NavLink>
    </>
  )
}

export default EditRoom;
