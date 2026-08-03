import { useEffect, useState } from "react";
import { NavLink, useNavigate } from "react-router";
import SensorSelect from "./SensorSelect";

function EditHome() {

  const navigate = useNavigate();

  const [outdoorTemperatureSensor, setOutdoorTemperatureSensor] = useState<string | undefined>(undefined);
  const [flowTemperatureSensor, setFlowTemperatureSensor] = useState<string | undefined>(undefined);
  const [returnTemperatureSensor, setReturnTemperatureSensor] = useState<string | undefined>(undefined);
  const [flowRateSensor, setFlowRateSensor] = useState<string | undefined>(undefined);

  useEffect(() => {
    fetch('/api/home').then(response => response.json()).then(data => {
      setOutdoorTemperatureSensor(`${data.outdoorTemperatureSensorNodeId}|${data.outdoorTemperatureSensorEndpointId}`);
      setFlowTemperatureSensor(`${data.heatSourceFlowTemperatureSensorNodeId}|${data.heatSourceFlowTemperatureSensorEndpointId}`);
      setReturnTemperatureSensor(`${data.heatSourceReturnTemperatureSensorNodeId}|${data.heatSourceReturnTemperatureSensorEndpointId}`);
      setFlowRateSensor(`${data.heatSourceFlowRateSensorNodeId}|${data.heatSourceFlowRateSensorEndpointId}`);
    });
  }, []);

  const save = (e: any) => {
    e.preventDefault();

    var outdoorTemperatureSensorNodeId = parseInt(outdoorTemperatureSensor!.split('|')[0]);
    var outdoorTemperatureSensorEndpointId = parseInt(outdoorTemperatureSensor!.split('|')[1]);

    var flowTemperatureSensorNodeId = parseInt(flowTemperatureSensor!.split('|')[0]);
    var flowTemperatureSensorEndpointId = parseInt(flowTemperatureSensor!.split('|')[1]);

    var returnTemperatureSensorNodeId = parseInt(returnTemperatureSensor!.split('|')[0]);
    var returnTemperatureSensorEndpointId = parseInt(returnTemperatureSensor!.split('|')[1]);

    var flowRateSensorNodeId = parseInt(flowRateSensor!.split('|')[0]);
    var flowRateSensorEndpointId = parseInt(flowRateSensor!.split('|')[1]);

    var object: any = {
      outdoorTemperatureSensorNodeId,
      outdoorTemperatureSensorEndpointId,
      flowTemperatureSensorNodeId,
      flowTemperatureSensorEndpointId,
      returnTemperatureSensorNodeId,
      returnTemperatureSensorEndpointId,
      flowRateSensorNodeId,
      flowRateSensorEndpointId
    };
    var json = JSON.stringify(object);

    fetch(`/api/home`, { method: "PUT", headers: { 'Content-Type': 'application/json' }, body: json }).then(r => {
      if (r.ok) {
        navigate('/');
      } else {
        alert("Failed to update home");
      }
    });
  }

  return (
    <>
      <h1>Edit Home</h1>
      <hr />
      <div className="mb-3">
        <SensorSelect deviceType={770} title="Outdoor Temperature Sensor" required={false} selectedSensor={outdoorTemperatureSensor} onSelectedSensorChange={(e: string) => setOutdoorTemperatureSensor(e)} />
      </div>
      <div className="mb-3">
        <SensorSelect deviceType={770} title="Flow Temperature Sensor" required={false} selectedSensor={flowTemperatureSensor} onSelectedSensorChange={(e: string) => setFlowTemperatureSensor(e)} />
      </div>
      <div className="mb-3">
        <SensorSelect deviceType={770} title="Return Temperature Sensor" required={false} selectedSensor={returnTemperatureSensor} onSelectedSensorChange={(e: string) => setReturnTemperatureSensor(e)} />
      </div>
      <div className="mb-3">
        <SensorSelect deviceType={774} title="Flow Rate Sensor" required={false} selectedSensor={flowRateSensor} onSelectedSensorChange={(e: string) => setFlowRateSensor(e)} />
      </div>
      <button className="btn btn-primary" onClick={save} style={{ 'marginRight': '5px' }}>Save</button>
      <NavLink className="btn btn-danger" to={`/`}>Cancel</NavLink>
    </>
  )
}

export default EditHome;
