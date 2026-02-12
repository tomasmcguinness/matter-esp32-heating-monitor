import { useState } from "react";
import { NavLink, useNavigate } from "react-router";
import SensorSelect from "./SensorSelect";

function AddRoom() {

  const navigate = useNavigate();

  const [name, setName] = useState<string | undefined>(undefined);
  const [temperatureSensor, setTemperatureSensor] = useState<string | undefined>(undefined);
  const [heatLossPerDegree, setHeatLossPerDegree] = useState<string | undefined>(undefined);
  const [targetTemperature, setTargetTemperature] = useState<number | undefined>(undefined);

  function handleSubmit(e: any) {
    e.preventDefault();

    var temperatureSensorNodeId = parseInt(temperatureSensor!.split('|')[0]);
    var temperatureSensorEndpointId = parseInt(temperatureSensor!.split('|')[1]);

    var object: any = {
      name,
      targetTemperature: targetTemperature! * 100,
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
          <label htmlFor="targetTemperature" className="form-label">Target Temperature <span style={{ 'color': 'red' }}>*</span></label>
          <input type="number" name="targetTemperature" maxLength={20} className="form-control" id="targetTemperature" placeholder="20" required={true} value={targetTemperature || ''} onChange={(e) => setTargetTemperature(parseInt(e.target.value))} />
        </div>
        <div className="mb-3">
          <label htmlFor="heatLoss" className="form-label">Calculated Heat Loss (W/°C) <span style={{ 'color': 'red' }}>*</span></label>
          <input type="number" name="heatLoss" maxLength={20} className="form-control" id="heatLoss" placeholder="25" required={true} value={heatLossPerDegree || ''} onChange={(e) => setHeatLossPerDegree(e.target.value)} />
        </div>
        <div className="mb-3">
          <SensorSelect deviceType={770} title="Room Temperature Sensor" required={true} selectedSensor={temperatureSensor || ''} onSelectedSensorChange={(e) => setTemperatureSensor(e)} />
        </div>
        <button type="submit" className="btn btn-primary" style={{ 'marginRight': '5px' }}>Add Room</button>
        <NavLink className="btn btn-cancel" to="/rooms">Cancel</NavLink>
      </form>
    </>
  )
}

export default AddRoom;
