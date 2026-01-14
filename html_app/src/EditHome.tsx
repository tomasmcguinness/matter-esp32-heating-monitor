import { useEffect, useState } from "react";
import { NavLink } from "react-router";
import SensorSelect from "./SensorSelect";

function EditHome() {

  const [outdoorTemperatureSensor, setOutdoorTemperatureSensor] = useState<string | undefined>(undefined);

  useEffect(() => {
    fetch('/api/home').then(response => response.json()).then(data => {
      setOutdoorTemperatureSensor(`${data.outdoorTemperatureSensorNodeId}|${data.outdoorTemperatureSensorEndpointId}`);
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

  return (
    <>
      <h1>Edit Home</h1>
      <hr />
      <div className="mb-3">
        <SensorSelect title="Outdoor Temperature Sensor" selectedSensor={outdoorTemperatureSensor} onSelectedSensorChange={(e:string) => setOutdoorTemperatureSensor(e)} />
      </div>
      <button className="btn btn-primary" onClick={save} style={{ 'marginRight': '5px' }}>Save</button>
      <NavLink className="btn btn-danger" to={`/`}>Cancel</NavLink>
    </>
  )
}

export default EditHome;
