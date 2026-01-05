import { useEffect, useState } from "react";
import { NavLink } from "react-router";

function EditHome() {

  const [sensors, setSensors] = useState<[]>([]);

  const [outdoorTemperatureSensor, setOutdoorTemperatureSensor] = useState<string | undefined>(undefined);

  useEffect(() => {
    fetch('/api/sensors').then(response => response.json()).then(data => {
      setSensors(data);

      fetch('/api/home').then(response => response.json()).then(data => {
        setOutdoorTemperatureSensor(`${data.outdoorTemperatureSensorNodeId}|${data.outdoorTemperatureSensorEndpointId}`);
      });
    });
  }, []);

  const save = (e: any) => {
    e.preventDefault();

    var outdoorTemperatureSensorNodeId = parseInt(outdoorTemperatureSensor!.split('|')[0]);
    var outdoorTemperatureSensorEndpointId = parseInt(outdoorTemperatureSensor!.split('|')[1]);

    var object: any = {
      outdoorTemperatureSensorNodeId,
      outdoorTemperatureSensorEndpointId
    };
    var json = JSON.stringify(object);

    fetch(`/api/home`, { method: "PUT", headers: { 'Content-Type': 'application/json' }, body: json });
  }

  let sensorOptions = sensors.map((s: any) => {
    var key = `${s.nodeId}|${s.endpointId}`;
    return <option key={key} value={key}>{s.vendorName}/{s.productName} (0x{s.nodeId} - 0x{s.endpointId})</option>;
  });

  return (
    <>
      <h1>Edit Home</h1>
      <hr />
      <div className="mb-3">
        <label htmlFor="outdoorTemperatureSensor" className="form-label">Outdoor Temperature Sensor <span style={{ 'color': 'red' }}>*</span></label>
        <select name="outdoorTemperatureSensor" className="form-control" id="temperatureSensor" value={outdoorTemperatureSensor || ''} onChange={(e) => setOutdoorTemperatureSensor(e.target.value)} required={true}>
          <option value=''></option>
          {sensorOptions}
        </select>
      </div>
      <button className="btn btn-primary" onClick={save} style={{ 'marginRight': '5px' }}>Save</button>
      <NavLink className="btn btn-danger" to={`/`}>Cancel</NavLink>
    </>
  )
}

export default EditHome;
