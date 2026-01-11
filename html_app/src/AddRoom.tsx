import { useEffect, useState } from "react";
import { NavLink, useNavigate } from "react-router";

function AddRoom() {

  let navigate = useNavigate();

  const [name, setName] = useState<string | undefined>(undefined);
  const [temperatureSensor, setTemperatureSensor] = useState<string | undefined>(undefined);
  const [heatLossPerDegree, setHeatLossPerDegree] = useState<string | undefined>(undefined);
  const [sensors, setSensors] = useState<[]>([]);

  useEffect(() => {
    fetch('/api/sensors').then(response => response.json()).then(data => setSensors(data));
  }, []);

  function handleSubmit(e: any) {
    e.preventDefault();

    var temperatureSensorNodeId = parseInt(temperatureSensor!.split('|')[0]);
    var temperatureSensorEndpointId = parseInt(temperatureSensor!.split('|')[1]);

    var object: any = {
      name,
      temperatureSensorNodeId,
      temperatureSensorEndpointId,
      heatLossPerDegree: parseInt(heatLossPerDegree!)
    };

    var json = JSON.stringify(object);

    fetch('/api/rooms', { method: "POST", headers: { 'Content-Type': 'application/json' }, body: json }).then(r => {
      if (r.ok) {
        navigate('/rooms');
      } else {
        alert("Failed to add the room");
      }
    });
  }

  let sensorOptions = sensors.map((s: any) => {
    var key = `${s.nodeId}|${s.endpointId}`;
    return <option key={key} value={key}>{s.vendorName}/{s.productName} (0x{s.nodeId} - 0x{s.endpointId})</option>;
  })

  return (
    <>
      <h1>Add Room</h1>
      <hr />
      <form method="post" onSubmit={handleSubmit}>
        <div className="mb-3">
          <label htmlFor="name" className="form-label">Name <span style={{ 'color': 'red' }}>*</span></label>
          <input type="text" name="name" maxLength={20} className="form-control" id="name" placeholder="Office" required={true} value={name || ''} onChange={(e) => setName(e.target.value)} />
        </div>
        <div className="mb-3">
          <label htmlFor="heatLoss" className="form-label">Heat Loss Per Degree <span style={{ 'color': 'red' }}>*</span></label>
          <input type="number" name="heatLoss" maxLength={20} className="form-control" id="heatLoss" placeholder="25" required={true} value={heatLossPerDegree || ''} onChange={(e) => setHeatLossPerDegree(e.target.value)} />
        </div>
        <div className="mb-3">
          <label htmlFor="temperatureSensor" className="form-label">Room Temperature Sensor <span style={{ 'color': 'red' }}>*</span></label>
          <select name="temperatureSensor" className="form-control" id="temperatureSensor" value={temperatureSensor || ''} onChange={(e) => setTemperatureSensor(e.target.value)} required={true}>
            <option></option>
            {sensorOptions}
          </select>
        </div>
        <button type="submit" className="btn btn-primary" style={{ 'marginRight': '5px' }}>Add Room</button>
        <NavLink className="btn btn-default" to="/rooms">Back</NavLink>
      </form>
    </>
  )
}

export default AddRoom;
